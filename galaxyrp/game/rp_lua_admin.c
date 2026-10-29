/*
===========================================================================
GLX_U: [Lua] /scripts for admins with the Scripts power, the rcon commands, and the error notices.
Loads, unloads, reloads and disables are queued for the end of the frame (rp_lua_core.c): nothing here
closes a state.
===========================================================================
*/
#include "rp_lua_local.h"

extern qboolean check_admin_command( gentity_t *ent, int admin_command, qboolean with_message );	// g_cmds.c
extern char *ConcatArgs( int start );

static void RP_LuaAdmin_Out( gentity_t *ent, const char *text )
{
	if ( ent )
		trap->SendServerCommand( ent - g_entities, va( "print \"%s\"", text ) );
	else
		trap->Print( "%s", text );
}

void RP_LuaAdmin_Notice( rpLuaPlugin_t *p, const char *text )
{
	int i, bit = RP_AdminBitByTitle( "Scripts" );
	char line[RP_LUA_TEXT_MAX + 1];

	(void)p;
	if ( bit < 0 )
		return;
	Q_strncpyz( line, text, sizeof( line ) );
	for ( i = 0; line[i]; i++ )
	{
		if ( line[i] == '"' )
			line[i] = '\'';
	}
	for ( i = 0; i < level.maxclients; i++ )
	{
		gentity_t *ent = &g_entities[i];

		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ( ent->r.svFlags & SVF_BOT ) )
			continue;
		if ( !ent->client->sess.loggedin || !( ent->client->pers.bitvalue & ( 1 << bit ) ) )
			continue;
		trap->SendServerCommand( i, va( "print \"^3[scripts] ^7%s\n\"", line ) );
	}
}

static void RP_LuaAdmin_List( gentity_t *ent )
{
	int i, n = 0;

	RP_LuaAdmin_Out( ent, va( "^3Scripts ^7(rp_lua %d, %d KB per plugin, rp_lua_plugins: %s)\n", rp_lua.integer, rp_lua_memkb.integer, rp_lua_plugins.string[0] ? rp_lua_plugins.string : "none" ) );
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse )
			continue;
		n++;
		if ( p->L )
			RP_LuaAdmin_Out( ent, va( "^3%s^7: running, %d KB (peak %d), %d errors, %d timers, %d commands, %d watched\n",
				p->name, (int)( p->memUsed / 1024 ), (int)( p->memPeak / 1024 ), p->errorsTotal, p->numTimers, p->numCommands, p->numWatches ) );
		else
			RP_LuaAdmin_Out( ent, va( "^3%s^7: ^1off^7 (%d errors)\n", p->name, p->errorsTotal ) );
	}
	if ( !n )
		RP_LuaAdmin_Out( ent, "No plugin is loaded.\n" );
}

static void RP_LuaAdmin_Stats( gentity_t *ent, const char *name )
{
	int i;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse || ( name[0] && Q_stricmp( p->name, name ) ) )
			continue;
		RP_LuaAdmin_Out( ent, va( "^3%s^7: %d calls, %d ms in all, %d errors; sent %d, refused %d (bucket) + %d (player busy); timers deferred %d; tokens now %d; stored %d KB\n",
			p->name, p->calls, p->callMs, p->errorsTotal, p->sent, p->refusedTokens, p->refusedRecipients, p->deferredTimers, p->tokens, (int)( p->storeBytes / 1024 ) ) );
		if ( p->L )
			RP_LuaAdmin_Out( ent, va( "   world: %d props, %d NPCs and %d vehicles alive (%d, %d and %d spawned this map, %d of the props dialogues, %d refused); %d uses, %d effects, %d sounds\n",
				RP_LuaWorld_CountLive( p, qfalse ), RP_LuaWorld_CountLive( p, qtrue ), RP_LuaWorld_CountVehicles( p ), p->propSpawns, p->npcSpawns, p->vehicleSpawns,
				p->dialogSpawns, p->spawnsRefused, p->uses, p->effects, p->sounds ) );
		if ( p->L && ( p->usesBusy || p->cmdsHeld || p->dbRefused || p->eventsShort || p->eventsSkipped || p->luaUsesHeld ) )
			RP_LuaAdmin_Out( ent, va( "   guards: %d uses refused (player busy), %d commands held back, %d payouts skipped (database writes), %d target_lua uses refused; past the frame budget: %d event calls shortened, %d not run\n",
				p->usesBusy, p->cmdsHeld, p->dbRefused, p->luaUsesHeld, p->eventsShort, p->eventsSkipped ) );
	}
}

static void RP_LuaAdmin_Help( gentity_t *ent )
{
	int i, k, n = 0;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse || !p->L )
			continue;
		for ( k = 0; k < p->numCommands; k++ )
		{
			RP_LuaAdmin_Out( ent, va( "^3/%s^7: %s ^9(%s)\n", p->cmdNames[k], p->cmdHelp[k][0] ? p->cmdHelp[k] : "no description", p->name ) );
			n++;
		}
	}
	if ( !n )
		RP_LuaAdmin_Out( ent, "No plugin adds a command.\n" );
}

/*
==================
The three actions, shared by /scripts and target_lua: each only queues (nothing here opens or closes a
state), and answers with the line the admin sees or the log gets
==================
*/
static const char *RP_LuaAdmin_Load( const char *arg )
{
	rpLuaPlugin_t *p = RP_Lua_Find( arg );

	if ( p && p->L )
		return va( "%s is already running.\n", p->name );
	if ( !RP_Lua_QueueLoad( arg ) )
		return "That is not a plugin name (letters, digits, '_' and '-', up to 32).\n";
	return va( "Loading %s at the end of this frame; see the log if it does not appear in /scripts list.\n", arg );
}

static const char *RP_LuaAdmin_Unload( const char *arg, rpLuaOp_t op )
{
	rpLuaPlugin_t *p = RP_Lua_Find( arg );

	if ( p && !p->L && op == RPLOP_UNLOAD )
	{ // switched off: nothing to close, the slot goes
		memset( p, 0, sizeof( *p ) );
		return va( "%s unloaded.\n", arg );
	}
	if ( !RP_Lua_QueueOp( arg, op ) )
		return va( "%s is not running.\n", arg );
	return va( "%s will be %s at the end of this frame.\n", arg, op == RPLOP_UNLOAD ? "unloaded" : "switched off" );
}

static const char *RP_LuaAdmin_Reload( const char *arg )
{
	int i, n = 0;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse || ( Q_stricmp( arg, "all" ) && Q_stricmp( p->name, arg ) ) )
			continue;
		if ( p->L )
			p->pendingOp = RPLOP_RELOAD;
		else
			RP_Lua_QueueLoad( p->name );
		n++;
	}
	return n ? va( "%d plugin%s reloading at the end of this frame.\n", n, n == 1 ? "" : "s" ) : va( "%s is not loaded.\n", arg );
}

// the shared body: ent NULL for rcon
static void RP_LuaAdmin_Scripts( gentity_t *ent, const char *sub, const char *arg )
{
	if ( !sub[0] || !Q_stricmp( sub, "list" ) )
		RP_LuaAdmin_List( ent );
	else if ( !Q_stricmp( sub, "stats" ) )
		RP_LuaAdmin_Stats( ent, arg );
	else if ( !Q_stricmp( sub, "help" ) )
		RP_LuaAdmin_Help( ent );
	else if ( !Q_stricmp( sub, "load" ) || !Q_stricmp( sub, "enable" ) )
		RP_LuaAdmin_Out( ent, RP_LuaAdmin_Load( arg ) );
	else if ( !Q_stricmp( sub, "unload" ) || !Q_stricmp( sub, "disable" ) )
		RP_LuaAdmin_Out( ent, RP_LuaAdmin_Unload( arg, Q_stricmp( sub, "unload" ) ? RPLOP_DISABLE : RPLOP_UNLOAD ) );
	else if ( !Q_stricmp( sub, "reload" ) )
		RP_LuaAdmin_Out( ent, RP_LuaAdmin_Reload( arg ) );
	else
		RP_LuaAdmin_Out( ent, "^3/scripts ^7[list] | stats [name] | help | load <name> | unload <name> | reload <name|all> | enable <name> | disable <name>\n" );
}

void Cmd_Scripts_f( gentity_t *ent )
{
	char sub[32] = { 0 }, arg[64] = { 0 };
	int bit = RP_AdminBitByTitle( "Scripts" );

	if ( bit < 0 || !check_admin_command( ent, bit, qtrue ) )
		return;
	if ( !rp_lua.integer )
	{
		RP_LuaAdmin_Out( ent, "Scripts are off (rp_lua 0).\n" );
		return;
	}
	trap->Argv( 1, sub, sizeof( sub ) );
	trap->Argv( 2, arg, sizeof( arg ) );
	RP_LuaAdmin_Scripts( ent, sub, arg );
}

/*
==================
rcon
==================
*/
void Svcmd_LuaList_f( void )
{
	char sub[32] = { 0 }, arg[64] = { 0 };

	trap->Argv( 1, sub, sizeof( sub ) );
	trap->Argv( 2, arg, sizeof( arg ) );
	if ( !rp_lua.integer )
	{
		trap->Print( "Scripts are off (rp_lua 0).\n" );
		return;
	}
	RP_LuaAdmin_Scripts( NULL, sub, arg );	// lua_list [stats|help|load|...]: the whole /scripts from the console
}

void Svcmd_LuaReload_f( void )
{
	char arg[64] = { 0 };

	trap->Argv( 1, arg, sizeof( arg ) );
	if ( !rp_lua.integer )
	{
		trap->Print( "Scripts are off (rp_lua 0).\n" );
		return;
	}
	RP_LuaAdmin_Scripts( NULL, "reload", arg[0] ? arg : "all" );
}

// lua <code>: the code runs in a throwaway plugin state under the same sandbox and budget, its results
// printed, and the state closed here -- safe, since only the console command is on the stack
void Svcmd_Lua_f( void )
{
	rpLuaPlugin_t *p = NULL;
	char code[MAX_STRING_CHARS];
	int i, top, n;

	if ( !rp_lua.integer )
	{
		trap->Print( "Scripts are off (rp_lua 0).\n" );
		return;
	}
	if ( rpLuaCurrent )
		return;
	Q_strncpyz( code, ConcatArgs( 1 ), sizeof( code ) );
	if ( !code[0] )
	{
		trap->Print( "lua <code>\n" );
		return;
	}
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		if ( !rpLuaPlugins[i].inUse )
		{
			p = &rpLuaPlugins[i];
			break;
		}
	}
	if ( !p )
	{
		trap->Print( "No free plugin slot.\n" );
		return;
	}
	if ( !RP_Lua_OpenScratch( p, "rcon" ) )
	{
		trap->Print( "Could not create the state.\n" );
		return;
	}
	if ( luaL_loadbufferx( p->L, code, strlen( code ), "=rcon", "t" ) != LUA_OK )
	{
		trap->Print( "%s\n", lua_tostring( p->L, -1 ) );
		RP_Lua_CloseScratch( p );
		return;
	}
	top = lua_gettop( p->L ) - 1;
	if ( RP_Lua_Call( p, 0, LUA_MULTRET ) )
	{
		n = lua_gettop( p->L ) - top;
		for ( i = 1; i <= n; i++ )
		{
			const char *s = luaL_tolstring( p->L, top + i, NULL );

			trap->Print( "%s%s", s, i < n ? "\t" : "\n" );
			lua_pop( p->L, 1 );
		}
	}
	else
		trap->Print( "(error: see the log)\n" );
	RP_Lua_CloseScratch( p );
}

/*
==================
target_lua

GLX_U: [Lua] a map entity that does what /scripts load | unload | reload <name> does, when it is used -- by a
button, a trigger, a relay, a dialogue option, ICARUS, /entuse. "plugin" is the folder name, checked as
/scripts checks it; "action" is load (the default), unload or reload. Spawnflags: 1 ONCE does it once per
map, 128 INACTIVE as every target. It fires no target of its own: a load is only queued here, and the
plugin's init is where what happens next belongs.

It never runs script code and never touches a state: the use records the operation in the queue that
/scripts uses, and RP_Lua_RunQueue carries it out at the end of the frame, outside any use chain and
any script call (the first attempt's target_lua called into running scripts from the use chain; that is
the nested-call path v2 has no more). A script cannot set one off: World.use skips the class
(rp_lua_world.c), and a use made while a script call is on the stack -- a chain a World.use started --
is refused here and counted. Whoever placed it had the power to (the mapper, the host's entity file,
an admin with the Entity System power); /scripts keeps its own power check. With rp_lua off the use
logs and does nothing, so a map with target_luas runs as it always did.
==================
*/
#define RP_TLUA_ONCE	1

static const char *const rpTargetLuaActions[] = { "load", "unload", "reload", NULL };	// ent->count

void target_lua_use( gentity_t *self, gentity_t *other, gentity_t *activator )
{
	const char *who = ( activator && activator->client ) ? activator->client->pers.netname : "the map";
	const char *tn = self->targetname ? self->targetname : "unnamed";
	const char *said;

	(void)other;
	if ( self->flags & FL_INACTIVE )
		return;
	if ( RP_LUA_IN_CALL() )
	{ // a script's World.use, or a chain one started: plugins are chosen by people (rp_lua_plugins, /scripts, this entity)
		if ( rpLuaCurrent )
			rpLuaCurrent->luaUsesHeld++;
		G_LogPrintf( "target_lua %s (%d) refused: used from a script (%s)\n", tn, self->s.number, rpLuaCurrent ? rpLuaCurrent->name : "?" );
		return;
	}
	if ( !rp_lua.integer )
	{
		G_LogPrintf( "target_lua %s (%d) used by %s^7: rp_lua is off, nothing done\n", tn, self->s.number, who );
		return;
	}
	if ( ( self->spawnflags & RP_TLUA_ONCE ) && self->genericValue1 )
		return;
	G_ActivateBehavior( self, BSET_USE );
	switch ( self->count )
	{
	case 1:		said = RP_LuaAdmin_Unload( self->message, RPLOP_UNLOAD ); break;
	case 2:		said = RP_LuaAdmin_Reload( self->message ); break;
	default:	said = RP_LuaAdmin_Load( self->message ); break;
	}
	G_LogPrintf( "target_lua %s (%d) used by %s^7, %s %s: %s", tn, self->s.number, who, rpTargetLuaActions[self->count], self->message, said );
	if ( self->spawnflags & RP_TLUA_ONCE )
		self->genericValue1 = 1;
}

void SP_target_lua( gentity_t *self )
{
	char *s;
	int i;

	G_SpawnString( "plugin", "", &s );
	if ( !RP_Lua_ValidName( s ) )
	{
		G_LogPrintf( "target_lua at %s: \"%s\" is not a plugin name; not spawned\n", vtos( self->s.origin ), s );
		RP_SpawnSaysWhy( self, "a target_lua needs a plugin name (letters, digits, '_' and '-', up to 32)" );
		G_FreeEntity( self );
		return;
	}
	self->message = G_NewString( s );
	G_SpawnString( "action", "load", &s );
	for ( i = 0; rpTargetLuaActions[i]; i++ )
	{
		if ( !Q_stricmp( s, rpTargetLuaActions[i] ) )
			break;
	}
	if ( !rpTargetLuaActions[i] )
	{
		G_LogPrintf( "target_lua at %s: \"%s\" is not an action; not spawned\n", vtos( self->s.origin ), s );
		RP_SpawnSaysWhy( self, "a target_lua's action is load, unload or reload" );
		G_FreeEntity( self );
		return;
	}
	self->count = i;
	if ( RP_EntitySystemMade( self ) && i == 0 && !RP_Lua_PluginFileExists( self->message ) )
	{ // one the entity commands place: the file now, as a target_level_change's map is checked; the map's own is logged at load
		if ( RP_RefuseAtRuntime( self, va( "this server has no plugin \"%s\" (lua/plugins/%s/plugin.lua)", self->message, self->message ) ) )
			return;
	}
	self->genericValue1 = 0;
	if ( self->spawnflags & 128 )
		self->flags |= FL_INACTIVE;
	G_SetOrigin( self, self->s.origin );
	self->use = target_lua_use;
}
