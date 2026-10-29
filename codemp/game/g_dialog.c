/*
===========================================================================
GLX_U: [Dialogues] target_dialog and target_flag, and the one dialogue a player may have open.

A target_dialog is used like any target: its activator, a player, is shown a title, a text and up to six
numbered options -- in the console area for everyone, and as a menu for a client that says "rpdlg 1" in
its userinfo. The player answers with /dialog <n> (the menu sends "dialog <n> <id>"), and the option's
"go<n>" targetname is fired with the player as activator through G_UseTargets2(): exactly what a button
press does, so another target_dialog, a target_reward, a target_level_change, a target_kill or a door
behave as they do behind a button. A dialogue closes by itself on a timeout, when the player walks away
from its speaker, dies, is knocked down, goes spectator, leaves, or opens another.

target_flag sets or clears a named flag on its activator, kept per player for this connection and map
(clientPersistant_t::rpFlags): a dialogue's "need<n>" offers an option only to players with (or, with
"!", without) that flag. That is how a map keeps quest state per player without a script or a database.

A plugin's World.use (rp_lua_world.c) may open one for its player, when that player's command budget for
script calls has room (RP_LUA_CMD_ROOM, rp_lua_api.c); the answer still comes from the player's own /dialog.
===========================================================================
*/
#include "g_local.h"

extern qboolean check_admin_command( gentity_t *ent, int admin_command, qboolean with_message );	// g_cmds.c
qboolean BG_InKnockDown( int anim );	// bg_pmove.c

#define RP_DLG_TITLE_MAX		48
#define RP_DLG_TEXT_MAX			255		// one menu cvar on the client
#define RP_DLG_OPTION_MAX		64		// one menu line
#define RP_DLG_OPTIONS			6
#define RP_DLG_WORD_MAX			40		// a run without a space longer than this is broken (the menu's wrap)
#define RP_DLG_TIMEOUT_DEFAULT	60
#define RP_DLG_TIMEOUT_MIN		5
#define RP_DLG_TIMEOUT_MAX		600
#define RP_DLG_RANGE_DEFAULT	256.0f
#define RP_DLG_CHECK_MS			500		// how often an open dialogue's timeout, range and player are looked at
#define RP_DLG_ONCE_EACH		1		// spawnflags
#define RP_DLG_NO_CANCEL_TEXT	4

typedef enum {
	RPDLG_ANSWERED, RPDLG_CANCELLED, RPDLG_TIMEOUT, RPDLG_AWAY, RPDLG_DIED, RPDLG_LEFT, RPDLG_REPLACED, RPDLG_GONE
} rpDialogReason_t;

static const char *rpDialogReasonText[] = {
	"answered", "cancelled", "timeout", "away", "died", "left", "replaced", "gone"
};

static int rpDialogNextId = 1;

/*
==================
Text rules: quotes become ', line breaks spaces, a long run without a space is broken (the menu wraps at
spaces), a leading '@' is covered (a string-table lookup on the client), then cut to the limit.
==================
*/
static void RP_DialogClean( char *text, int size, int max )
{
	int i, run = 0, len;
	char tmp[512];

	for ( i = 0; text[i]; i++ )
	{
		if ( text[i] == '"' )
			text[i] = '\'';
		else if ( text[i] == '\n' || text[i] == '\r' || text[i] == '\t' )
			text[i] = ' ';
	}
	if ( text[0] == '@' && size > 3 )
	{
		Q_strncpyz( tmp, text, sizeof( tmp ) );
		Com_sprintf( text, size, "^7%s", tmp );
	}
	// break long runs: tmp holds the result
	len = 0;
	for ( i = 0; text[i] && len < (int)sizeof( tmp ) - 2; i++ )
	{
		if ( text[i] == ' ' )
			run = 0;
		else if ( ++run > RP_DLG_WORD_MAX )
		{
			tmp[len++] = ' ';
			run = 1;
		}
		tmp[len++] = text[i];
	}
	tmp[len] = '\0';
	if ( max >= size )
		max = size - 1;
	Q_strncpyz( text, tmp, max + 1 );
}

/*
==================
Flags (target_flag, need<n>)
==================
*/
static qboolean RP_FlagNameOk( const char *name )
{
	int i;

	if ( !name || !name[0] || strlen( name ) > RP_FLAG_NAME_MAX )
		return qfalse;
	for ( i = 0; name[i]; i++ )
	{
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
			return qfalse;
	}
	return qtrue;
}

qboolean RP_FlagHas( const gentity_t *ent, const char *name )
{
	int i;

	if ( !ent || !ent->client )
		return qfalse;
	for ( i = 0; i < RP_FLAGS_PER_PLAYER; i++ )
	{
		if ( ent->client->pers.rpFlags[i][0] && !Q_stricmp( ent->client->pers.rpFlags[i], name ) )
			return qtrue;
	}
	return qfalse;
}

static qboolean RP_FlagSet( gentity_t *ent, const char *name )
{
	int i;

	if ( RP_FlagHas( ent, name ) )
		return qtrue;
	for ( i = 0; i < RP_FLAGS_PER_PLAYER; i++ )
	{
		if ( !ent->client->pers.rpFlags[i][0] )
		{
			Q_strncpyz( ent->client->pers.rpFlags[i], name, sizeof( ent->client->pers.rpFlags[i] ) );
			return qtrue;
		}
	}
	return qfalse;
}

static void RP_FlagClear( gentity_t *ent, const char *name )
{
	int i;

	for ( i = 0; i < RP_FLAGS_PER_PLAYER; i++ )
	{
		if ( ent->client->pers.rpFlags[i][0] && !Q_stricmp( ent->client->pers.rpFlags[i], name ) )
			ent->client->pers.rpFlags[i][0] = '\0';
	}
}

// "name" must be held, "!name" must not; an empty need is always met
static qboolean RP_FlagNeedMet( const gentity_t *ent, const char *need )
{
	if ( !need || !need[0] )
		return qtrue;
	if ( need[0] == '!' )
		return RP_FlagHas( ent, need + 1 ) ? qfalse : qtrue;
	return RP_FlagHas( ent, need );
}

/*
==================
target_flag
==================
*/
void target_flag_use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
	if ( self->flags & FL_INACTIVE )
		return;
	if ( !activator || !activator->inuse || !activator->client || activator->s.number < 0 || activator->s.number >= MAX_CLIENTS )
		return;
	if ( self->spawnflags & 1 )
		RP_FlagClear( activator, self->message );
	else if ( !RP_FlagSet( activator, self->message ) )
		G_LogPrintf( "target_flag %d: %s^7 has %d flags already, '%s' not set\n", self->s.number, activator->client->pers.netname, RP_FLAGS_PER_PLAYER, self->message );
	G_UseTargets( self, activator );
}

void SP_target_flag( gentity_t *self )
{
	char *s;

	G_SpawnString( "flag", "", &s );
	if ( !RP_FlagNameOk( s ) )
	{
		G_LogPrintf( "target_flag %d: 'flag' is letters, digits and '_', up to %d (got '%s'); removed\n", self->s.number, RP_FLAG_NAME_MAX, s );
		RP_SpawnSaysWhy( self, "a target_flag needs a flag name (letters, digits and '_')" );
		G_FreeEntity( self );
		return;
	}
	self->message = G_NewString( s );
	if ( self->spawnflags & 128 )
		self->flags |= FL_INACTIVE;
	self->use = target_flag_use;
}

/*
==================
target_dialog
==================
*/
void SP_target_dialog( gentity_t *self )
{
	rpDialogDef_t *d;
	char *s;
	float f;
	int i;

	self->rpDialogDef = NULL;
	if ( !G_AllocRoomFor( sizeof( *d ) ) )
	{
		G_LogPrintf( "target_dialog %d: no room in the memory pool; removed\n", self->s.number );
		RP_SpawnSaysWhy( self, "the game's memory pool is nearly full" );
		G_FreeEntity( self );
		return;
	}
	d = (rpDialogDef_t *)G_Alloc( sizeof( *d ) );
	memset( d, 0, sizeof( *d ) );

	G_SpawnString( "title", "", &s );
	Q_strncpyz( d->title, s, sizeof( d->title ) );
	RP_DialogClean( d->title, sizeof( d->title ), RP_DLG_TITLE_MAX );
	G_SpawnString( "message", "", &s );
	Q_strncpyz( d->text, s, sizeof( d->text ) );
	RP_DialogClean( d->text, sizeof( d->text ), RP_DLG_TEXT_MAX );
	if ( strlen( s ) > RP_DLG_TEXT_MAX )
		G_LogPrintf( "target_dialog %d: message cut to %d characters\n", self->s.number, RP_DLG_TEXT_MAX );
	for ( i = 0; i < RP_DLG_OPTIONS; i++ )
	{
		G_SpawnString( va( "option%d", i + 1 ), "", &s );
		if ( !s[0] )
			break;
		Q_strncpyz( d->option[i], s, sizeof( d->option[i] ) );
		RP_DialogClean( d->option[i], sizeof( d->option[i] ), RP_DLG_OPTION_MAX );
		G_SpawnString( va( "go%d", i + 1 ), "", &s );
		Q_strncpyz( d->go[i], s, sizeof( d->go[i] ) );
		G_SpawnString( va( "need%d", i + 1 ), "", &s );
		if ( s[0] && !RP_FlagNameOk( s[0] == '!' ? s + 1 : s ) )
		{
			G_LogPrintf( "target_dialog %d: need%d '%s' is not a flag name; ignored\n", self->s.number, i + 1, s );
			s = "";
		}
		Q_strncpyz( d->need[i], s, sizeof( d->need[i] ) );
	}
	d->numOptions = i;
	if ( !d->text[0] || !d->numOptions )
	{
		G_LogPrintf( "target_dialog %d (%s): needs a message and an option1; removed\n", self->s.number, self->targetname ? self->targetname : "unnamed" );
		RP_SpawnSaysWhy( self, "a target_dialog needs a message and an option1" );
		G_FreeEntity( self );
		return;
	}
	G_SpawnFloat( "timeout", va( "%d", RP_DLG_TIMEOUT_DEFAULT ), &f );
	if ( f < RP_DLG_TIMEOUT_MIN ) f = RP_DLG_TIMEOUT_MIN;
	if ( f > RP_DLG_TIMEOUT_MAX ) f = RP_DLG_TIMEOUT_MAX;
	d->timeoutMs = (int)( f * 1000 );
	G_SpawnFloat( "range", va( "%d", (int)RP_DLG_RANGE_DEFAULT ), &f );
	d->range = ( f > 0 ) ? f : 0;
	G_SpawnString( "speaker", "", &s );
	Q_strncpyz( d->speaker, s, sizeof( d->speaker ) );

	self->rpRewardSlots = NULL;
	if ( self->spawnflags & RP_DLG_ONCE_EACH )
	{ // the per-player record target_reward keeps: {enterTime, time shown}
		if ( !G_AllocRoomFor( MAX_CLIENTS * 2 * sizeof( int ) ) )
		{
			G_LogPrintf( "target_dialog %d: no room in the memory pool for its per-player record; removed\n", self->s.number );
			RP_SpawnSaysWhy( self, "the game's memory pool is nearly full" );
			G_FreeEntity( self );
			return;
		}
		self->rpRewardSlots = (int *)G_Alloc( MAX_CLIENTS * 2 * sizeof( int ) );
		memset( self->rpRewardSlots, 0, MAX_CLIENTS * 2 * sizeof( int ) );
	}
	self->rpDialogDef = d;
	if ( self->spawnflags & 128 )
		self->flags |= FL_INACTIVE;
	self->use = target_dialog_use;
}

/*
==================
The open dialogue of a player
==================
*/
static rpDialogOpen_t *RP_DialogOf( int clientNum )
{
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
		return NULL;
	return &level.rpDialog[clientNum];
}

// the entity behind an open dialogue, while it is still the same one
static gentity_t *RP_DialogEntity( const rpDialogOpen_t *o )
{
	gentity_t *e;

	if ( !o->id || o->entNum < MAX_CLIENTS || o->entNum >= MAX_ENTITIESTOTAL )
		return NULL;
	e = &g_entities[o->entNum];
	if ( !e->inuse || e->rpDialogDef != o->def )
		return NULL;
	return e;
}

// the console-area text everyone gets (menu players too: their console history keeps the talk)
static void RP_DialogShowText( gentity_t *player, const rpDialogOpen_t *o )
{
	char line[1024];
	int i;

	line[0] = '\0';
	if ( o->def->title[0] )
		Q_strcat( line, sizeof( line ), va( "^3%s\n", o->def->title ) );
	Q_strcat( line, sizeof( line ), va( "^7%s\n", o->def->text ) );
	for ( i = 0; i < o->numShown; i++ )
		Q_strcat( line, sizeof( line ), va( "^3%d.^7 %s\n", i + 1, o->def->option[o->shown[i]] ) );
	Q_strcat( line, sizeof( line ), "^9Type /dialog <number> to answer, /dialog 0 to walk away.\n" );
	trap->SendServerCommand( player->s.number, va( "print \"%s\"", line ) );
}

// the menu, for a client that has one: rpdlg <id> <seconds left> "title" "text" "option"...
static void RP_DialogShowMenu( gentity_t *player, const rpDialogOpen_t *o )
{
	char cmd[1024];
	int i, left = ( o->deadline - level.time + 999 ) / 1000;

	if ( left < 1 )
		left = 1;
	Com_sprintf( cmd, sizeof( cmd ), "rpdlg %d %d \"%s\" \"%s\"", o->id, left, o->def->title, o->def->text );
	for ( i = 0; i < o->numShown; i++ )
		Q_strcat( cmd, sizeof( cmd ), va( " \"%s\"", o->def->option[o->shown[i]] ) );
	trap->SendServerCommand( player->s.number, cmd );
}

static void RP_DialogShow( gentity_t *player, const rpDialogOpen_t *o )
{
	if ( player->client->pers.rpDialogMenu )
		RP_DialogShowMenu( player, o );
	RP_DialogShowText( player, o );
}

// closes the player's dialogue: tells the client, says so, fires the dialogue's own target for a walk-away
static void RP_DialogClose( int clientNum, rpDialogReason_t why )
{
	rpDialogOpen_t *o = RP_DialogOf( clientNum );
	gentity_t *player = &g_entities[clientNum], *e;
	int id;

	if ( !o || !o->id )
		return;
	id = o->id;
	e = RP_DialogEntity( o );
	o->id = 0;
	if ( !player->inuse || !player->client || player->client->pers.connected == CON_DISCONNECTED )
		return;
	if ( player->client->pers.rpDialogMenu && why != RPDLG_REPLACED )
		trap->SendServerCommand( clientNum, va( "rpdlgclose %d", id ) );
	if ( why != RPDLG_ANSWERED && why != RPDLG_REPLACED && !( e && ( e->spawnflags & RP_DLG_NO_CANCEL_TEXT ) ) )
		trap->SendServerCommand( clientNum, "print \"^9(the conversation is over)\n\"" );
	G_LogPrintf( "dialog %d closed for %s^7: %s\n", id, player->client->pers.netname, rpDialogReasonText[why] );
	if ( e && ( why == RPDLG_CANCELLED || why == RPDLG_TIMEOUT || why == RPDLG_AWAY ) && e->target && e->target[0] )
		G_UseTargets( e, player );
	if ( why != RPDLG_ANSWERED )	// an answer raises its own, after its option fired (Cmd_Dialog_f)
		RP_LUA_EVENT_DIALOG( player, e && e->targetname ? e->targetname : "", e ? e->s.number : -1, 0, NULL, rpDialogReasonText[why] );
}

static void RP_DialogOpen( gentity_t *self, gentity_t *player )
{
	rpDialogOpen_t *o = RP_DialogOf( player->s.number );
	rpDialogDef_t *d = self->rpDialogDef;
	int i, n = 0, shown[RP_DIALOG_OPTIONS];
	gentity_t *speaker = NULL;

	if ( !o || !d )
		return;
	for ( i = 0; i < d->numOptions; i++ )
	{ // GLX_U: built here, written into the record only once this dialogue really opens: a use that returns
	  // below (nothing to say, ONCE_EACH) must leave an open dialogue's option map as it is
		if ( RP_FlagNeedMet( player, d->need[i] ) )
			shown[n++] = i;
	}
	if ( !n )
		return;	// nothing this player may say: as if not used
	if ( self->rpRewardSlots )
	{ // ONCE_EACH: once per connection
		int *slot = &self->rpRewardSlots[player->s.number * 2];

		if ( slot[0] != player->client->pers.enterTime )
		{
			slot[0] = player->client->pers.enterTime;
			slot[1] = 0;
		}
		if ( slot[1] )
			return;
		slot[1] = level.time ? level.time : 1;
	}
	if ( o->id )
		RP_DialogClose( player->s.number, RPDLG_REPLACED );	// raises dialog_answer: a handler's own open is overwritten below, whole

	o->id = rpDialogNextId++;
	if ( rpDialogNextId <= 0 )
		rpDialogNextId = 1;
	o->entNum = self->s.number;
	o->def = d;
	o->numShown = n;
	memcpy( o->shown, shown, sizeof( o->shown ) );
	o->deadline = level.time + d->timeoutMs;
	o->nextCheck = level.time + RP_DLG_CHECK_MS;
	o->range = d->range;
	if ( d->speaker[0] )
		speaker = G_Find( NULL, FOFS( targetname ), d->speaker );
	VectorCopy( speaker ? speaker->r.currentOrigin : self->r.currentOrigin, o->origin );
	RP_DialogShow( player, o );
	G_LogPrintf( "dialog %d (%s, entity %d) opened for %s^7\n", o->id, self->targetname ? self->targetname : "unnamed", self->s.number, player->client->pers.netname );
}

void target_dialog_use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
	if ( self->flags & FL_INACTIVE || !self->rpDialogDef )
		return;
	if ( !activator || !activator->inuse || !activator->client || activator->s.number < 0 || activator->s.number >= MAX_CLIENTS ||
		activator->client->pers.connected != CON_CONNECTED || ( activator->r.svFlags & SVF_BOT ) )
		return;
	if ( !RP_LUA_CMD_ROOM( activator->s.number, 2 ) )
		return;	// GLX_U: [Lua] a script's use, the player's command budget spent: not opened, the open one untouched
	G_ActivateBehavior( self, BSET_USE );
	RP_DialogOpen( self, activator );
}

/*
==================
/dialog [n [id]]: answer n (1..6), 0 walks away, nothing shows the open dialogue again. The menu sends the
dialogue's id too, so a click that arrives after the dialogue changed does nothing.
==================
*/
void Cmd_Dialog_f( gentity_t *ent )
{
	rpDialogOpen_t *o = RP_DialogOf( ent->s.number );
	gentity_t *e;
	char arg[16], go[MAX_QPATH], option[65], name[MAX_QPATH];
	int n, id, num;

	if ( !o || !o->id || !( e = RP_DialogEntity( o ) ) )
	{
		if ( o && o->id )
			RP_DialogClose( ent->s.number, RPDLG_GONE );
		if ( trap->Argc() < 3 )	// the menu's late click says nothing
			trap->SendServerCommand( ent->s.number, "print \"No dialogue is open.\n\"" );
		return;
	}
	if ( trap->Argc() < 2 )
	{
		RP_DialogShow( ent, o );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	n = atoi( arg );
	if ( trap->Argc() >= 3 )
	{
		trap->Argv( 2, arg, sizeof( arg ) );
		id = atoi( arg );
		if ( id != o->id )
			return;	// an answer to a dialogue that is gone
	}
	if ( n == 0 )
	{
		RP_DialogClose( ent->s.number, RPDLG_CANCELLED );
		return;
	}
	if ( n < 1 || n > o->numShown )
	{
		trap->SendServerCommand( ent->s.number, va( "print \"There is no option %d. Type /dialog to see the dialogue again.\n\"", n ) );
		return;
	}
	trap->SendServerCommand( ent->s.number, va( "print \"^3> ^7%s\n\"", o->def->option[o->shown[n - 1]] ) );
	Q_strncpyz( go, o->def->go[o->shown[n - 1]], sizeof( go ) );
	Q_strncpyz( option, o->def->option[o->shown[n - 1]], sizeof( option ) );
	Q_strncpyz( name, e->targetname ? e->targetname : "", sizeof( name ) );
	num = e->s.number;
	G_LogPrintf( "dialog %d: %s^7 chose %d (%s)\n", o->id, ent->client->pers.netname, n, option );
	RP_DialogClose( ent->s.number, RPDLG_ANSWERED );
	if ( go[0] )
		G_UseTargets2( e, ent, go );	// as a button press: the player is the activator, the chain runs through GlobalUse
	RP_LUA_EVENT_DIALOG( ent, name, num, n, option, "answered" );	// GLX_U: [Lua] told after the option did its work
}

/*
==================
Every 500 ms: the timeout, the range, and the player's state
==================
*/
void RP_DialogFrame( void )
{
	int i;

	for ( i = 0; i < level.maxclients; i++ )
	{
		rpDialogOpen_t *o = &level.rpDialog[i];
		gentity_t *player = &g_entities[i];

		if ( !o->id || level.time < o->nextCheck )
			continue;
		o->nextCheck = level.time + RP_DLG_CHECK_MS;
		if ( !player->inuse || !player->client || player->client->pers.connected != CON_CONNECTED )
		{
			RP_DialogClose( i, RPDLG_LEFT );
			continue;
		}
		if ( !RP_DialogEntity( o ) )
		{
			RP_DialogClose( i, RPDLG_GONE );
			continue;
		}
		if ( player->health <= 0 || player->client->ps.pm_type == PM_DEAD || player->client->sess.sessionTeam == TEAM_SPECTATOR ||
			BG_InKnockDown( player->client->ps.legsAnim ) )
		{
			RP_DialogClose( i, RPDLG_DIED );
			continue;
		}
		if ( level.time >= o->deadline )
		{
			RP_DialogClose( i, RPDLG_TIMEOUT );
			continue;
		}
		if ( o->range > 0 && Distance( player->client->ps.origin, o->origin ) > o->range )
			RP_DialogClose( i, RPDLG_AWAY );
	}
}

// ClientDisconnect and ClientSpawn
void RP_DialogPlayerGone( int clientNum, qboolean disconnected )
{
	rpDialogOpen_t *o = RP_DialogOf( clientNum );

	if ( o && o->id )
		RP_DialogClose( clientNum, disconnected ? RPDLG_LEFT : RPDLG_DIED );
}

// a new map: nothing is open
void RP_DialogReset( void )
{
	memset( level.rpDialog, 0, sizeof( level.rpDialog ) );
}

/*
==================
/entflags <player>: the flags a player holds, for testing a map's quest logic (Entity System power)
==================
*/
void Cmd_EntFlags_f( gentity_t *ent )
{
	char arg[MAX_STRING_CHARS];
	int id, i, n = 0;
	char line[1024];

	if ( !check_admin_command( ent, ADM_ENTITYSYSTEM, qtrue ) )
		return;
	if ( trap->Argc() < 2 )
	{
		trap->SendServerCommand( ent->s.number, "print \"Usage: ^3/entflags <player name or ID>^7. The target_flag flags that player holds on this map.\n\"" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	id = ClientNumberFromString( ent, arg, qfalse );
	if ( id < 0 )
		return;
	line[0] = '\0';
	for ( i = 0; i < RP_FLAGS_PER_PLAYER; i++ )
	{
		if ( g_entities[id].client->pers.rpFlags[i][0] )
		{
			Q_strcat( line, sizeof( line ), va( "%s%s", n ? ", " : "", g_entities[id].client->pers.rpFlags[i] ) );
			n++;
		}
	}
	trap->SendServerCommand( ent->s.number, va( "print \"%s^7 holds %d flag%s%s%s.\n\"", g_entities[id].client->pers.netname, n, n == 1 ? "" : "s", n ? ": " : "", line ) );
}
