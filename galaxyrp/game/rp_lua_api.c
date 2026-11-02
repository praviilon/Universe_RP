/*
===========================================================================
GLX_U: [Lua] the API a plugin sees: Events, Msg, Commands, Timer, Players, World, Cvar, Log.

Everything that reaches a player goes through the command bucket (RP_LuaApi_TakeToken): a plugin is
paced like one more client, one token per sv_floodProtect ms with a burst of RP_LUA_TOKENS, and no
player receives more than RP_LUA_SENDS_PER_SEC script commands a second from all plugins together.
A refusal is returned as false, "why"; argument errors raise.

The stability guards, for what a script call sets off in the game (a World.use's chain): every command to
a client from inside a script call counts toward RP_LUA_CMDS_PER_SEC for that client (the engine drops a
client 128 reliable commands behind), and every database write toward RP_LUA_DB_WRITES_FRAME. Outside a
script call -- a player's own button, trigger, /dialog answer or kill -- nothing is counted.
===========================================================================
*/
#include "rp_lua_local.h"

#define RP_LUA_PLAYER_MT "rp.player"

typedef struct {
	int clientNum;
	int gen;
} rpLuaPlayer_t;

static int rpLuaSends[MAX_CLIENTS][RP_LUA_SENDS_PER_SEC];	// level.time of the last sends to each client

// the stability guards
static gameImport_t rpLuaTrap;		// the engine's import table, SendServerCommand guarded
static void ( *rpLuaEngineSend )( int clientNum, const char *text );
static int rpLuaCmdRing[MAX_CLIENTS][RP_LUA_CMDS_PER_5S];	// level.time + 1 of the last guarded commands (0: none)
static int rpLuaCmdHead[MAX_CLIENTS];	// the oldest of them
static int rpLuaChainLogged;		// level.time + 1 of the last "chain stopped" log line
static int rpLuaCmdLogged[MAX_CLIENTS];	// level.time + 1 of the last "held back" log line
static int rpLuaDbFrame, rpLuaDbWrites, rpLuaDbLogged;	// level.time + 1 of the frame counted, writes in it, logged in it

/*
==================
Players
==================
*/
void RP_LuaApi_PushPlayer( lua_State *L, int clientNum )
{
	rpLuaPlayer_t *h = (rpLuaPlayer_t *)lua_newuserdatauv( L, sizeof( *h ), 0 );

	h->clientNum = clientNum;
	h->gen = ( clientNum >= 0 && clientNum < MAX_CLIENTS ) ? rpLuaGen[clientNum] : 0;
	luaL_setmetatable( L, RP_LUA_PLAYER_MT );
}

static gentity_t *RP_LuaApi_Handle( lua_State *L, int idx, qboolean mustBeValid )
{
	rpLuaPlayer_t *h = (rpLuaPlayer_t *)luaL_checkudata( L, idx, RP_LUA_PLAYER_MT );
	gentity_t *ent;

	if ( h->clientNum >= 0 && h->clientNum < MAX_CLIENTS && h->gen == rpLuaGen[h->clientNum] )
	{
		ent = &g_entities[h->clientNum];
		if ( ent->inuse && ent->client && ent->client->pers.connected != CON_DISCONNECTED )
			return ent;
	}
	if ( mustBeValid )
		luaL_error( L, "the player has left" );
	return NULL;
}

qboolean RP_LuaApi_PlayerArg( lua_State *L, int idx, int *clientNum )
{
	gentity_t *ent = RP_LuaApi_Handle( L, idx, qtrue );

	*clientNum = ent - g_entities;
	return qtrue;
}

static int RP_LuaApi_PlayerValid( lua_State *L )
{
	lua_pushboolean( L, RP_LuaApi_Handle( L, 1, qfalse ) != NULL );
	return 1;
}

static int RP_LuaApi_PlayerIsAdmin( lua_State *L )
{
	gentity_t *ent = RP_LuaApi_Handle( L, 1, qtrue );
	const char *title = luaL_checkstring( L, 2 );
	int bit = RP_AdminBitByTitle( title );

	if ( bit < 0 )
		return luaL_error( L, "no admin power named '%s'", title );
	lua_pushboolean( L, ent->client->sess.loggedin && ( ent->client->pers.bitvalue & ( 1 << bit ) ) );
	return 1;
}

static const char *RP_LuaApi_TeamName( team_t t )
{
	switch ( t )
	{
	case TEAM_RED:			return "red";
	case TEAM_BLUE:			return "blue";
	case TEAM_SPECTATOR:	return "spectator";
	default:				return "free";
	}
}

static int RP_LuaApi_PlayerIndex( lua_State *L )
{
	const char *key = luaL_checkstring( L, 2 );
	gentity_t *ent;

	if ( !strcmp( key, "valid" ) )		{ lua_pushcfunction( L, RP_LuaApi_PlayerValid ); return 1; }
	if ( !strcmp( key, "is_admin" ) )	{ lua_pushcfunction( L, RP_LuaApi_PlayerIsAdmin ); return 1; }

	ent = RP_LuaApi_Handle( L, 1, qfalse );
	if ( !ent )
	{
		if ( !strcmp( key, "id" ) )
		{
			lua_pushinteger( L, ( (rpLuaPlayer_t *)lua_touserdata( L, 1 ) )->clientNum );
			return 1;
		}
		lua_pushnil( L );
		return 1;
	}

	if ( !strcmp( key, "id" ) )				lua_pushinteger( L, ent - g_entities );
	else if ( !strcmp( key, "name" ) )		lua_pushstring( L, ent->client->pers.netname );
	else if ( !strcmp( key, "clean_name" ) )
	{
		char clean[MAX_NETNAME];

		Q_strncpyz( clean, ent->client->pers.netname, sizeof( clean ) );
		Q_CleanStr( clean );
		lua_pushstring( L, clean );
	}
	else if ( !strcmp( key, "account" ) )
	{
		if ( ent->client->sess.loggedin )
			lua_pushstring( L, ent->client->sess.filename );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "char_id" ) )
	{
		if ( ent->client->sess.loggedin )
			lua_pushinteger( L, ent->client->pers.CharID );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "level" ) )		lua_pushinteger( L, ent->client->sess.loggedin ? ent->client->pers.level : 0 );
	else if ( !strcmp( key, "credits" ) )	lua_pushinteger( L, ent->client->sess.loggedin ? ent->client->pers.credits : 0 );
	else if ( !strcmp( key, "health" ) )	lua_pushinteger( L, ent->health );
	else if ( !strcmp( key, "team" ) )		lua_pushstring( L, RP_LuaApi_TeamName( ent->client->sess.sessionTeam ) );
	else if ( !strcmp( key, "is_bot" ) )	lua_pushboolean( L, ( ent->r.svFlags & SVF_BOT ) != 0 );
	else if ( !strcmp( key, "origin" ) )
	{
		lua_createtable( L, 0, 3 );
		lua_pushnumber( L, ent->client->ps.origin[0] ); lua_setfield( L, -2, "x" );
		lua_pushnumber( L, ent->client->ps.origin[1] ); lua_setfield( L, -2, "y" );
		lua_pushnumber( L, ent->client->ps.origin[2] ); lua_setfield( L, -2, "z" );
	}
	else
		lua_pushnil( L );
	return 1;
}

static int RP_LuaApi_PlayerEq( lua_State *L )
{
	rpLuaPlayer_t *a = (rpLuaPlayer_t *)luaL_checkudata( L, 1, RP_LUA_PLAYER_MT );
	rpLuaPlayer_t *b = (rpLuaPlayer_t *)luaL_checkudata( L, 2, RP_LUA_PLAYER_MT );

	lua_pushboolean( L, a->clientNum == b->clientNum && a->gen == b->gen );
	return 1;
}

static int RP_LuaApi_PlayerToString( lua_State *L )
{
	rpLuaPlayer_t *h = (rpLuaPlayer_t *)luaL_checkudata( L, 1, RP_LUA_PLAYER_MT );

	lua_pushfstring( L, "player(%d)", h->clientNum );
	return 1;
}

static qboolean RP_LuaApi_Counted( const gentity_t *ent )
{ // a player scripts may see and message: connected, begun, not a bot
	return ( ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED && !( ent->r.svFlags & SVF_BOT ) ) ? qtrue : qfalse;
}

static int RP_LuaApi_PlayersAll( lua_State *L )
{
	int i, n = 0;

	lua_newtable( L );
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( !RP_LuaApi_Counted( &g_entities[i] ) )
			continue;
		RP_LuaApi_PushPlayer( L, i );
		lua_rawseti( L, -2, ++n );
	}
	return 1;
}

static int RP_LuaApi_PlayersGet( lua_State *L )
{
	int id = (int)luaL_checkinteger( L, 1 );

	if ( id < 0 || id >= level.maxclients || !RP_LuaApi_Counted( &g_entities[id] ) )
		lua_pushnil( L );
	else
		RP_LuaApi_PushPlayer( L, id );
	return 1;
}

/*
==================
The bucket
==================
*/
qboolean RP_LuaApi_TakeToken( rpLuaPlugin_t *p, int n )
{
	RP_Lua_RefillTokens( p );
	if ( p->tokens < n )
	{
		p->refusedTokens++;
		return qfalse;
	}
	p->tokens -= n;
	return qtrue;
}

qboolean RP_LuaApi_SendRoom( int clientNum )
{
	int i, recent = 0;

	for ( i = 0; i < RP_LUA_SENDS_PER_SEC; i++ )
	{
		if ( rpLuaSends[clientNum][i] && level.time >= rpLuaSends[clientNum][i] && level.time - rpLuaSends[clientNum][i] < 1000 )
			recent++;
	}
	return ( recent < RP_LUA_SENDS_PER_SEC ) ? qtrue : qfalse;
}

void RP_LuaApi_Sent( int clientNum )
{
	int i, oldest = 0;

	for ( i = 1; i < RP_LUA_SENDS_PER_SEC; i++ )
	{
		if ( rpLuaSends[clientNum][i] < rpLuaSends[clientNum][oldest] )
			oldest = i;
	}
	rpLuaSends[clientNum][oldest] = level.time;
}

/*
==================
The stability guards
==================
*/
const char *RP_Lua_CurrentName( void )
{
	return rpLuaCurrent ? rpLuaCurrent->name : NULL;
}

// n more guarded commands to clientNum fit: with them, at most RP_LUA_CMDS_PER_SEC in the last second and
// RP_LUA_CMDS_PER_5S in the last five (the engine drops a client 128 unacknowledged commands behind; the second
// window keeps a client that stops acknowledging for a few seconds under that from script traffic alone)
static qboolean RP_Lua_CmdFits( int clientNum, int n )
{
	int k, inSecond = 0, inFive = 0;

	if ( n > RP_LUA_CMDS_PER_SEC )
		return qfalse;
	for ( k = 0; k < RP_LUA_CMDS_PER_5S; k++ )
	{
		const int t = rpLuaCmdRing[clientNum][k], age = level.time + 1 - t;

		if ( !t || age < 0 )
			continue;
		if ( age < 1000 )
			inSecond++;
		if ( age < 5000 )
			inFive++;
	}
	return ( inSecond + n <= RP_LUA_CMDS_PER_SEC && inFive + n <= RP_LUA_CMDS_PER_5S ) ? qtrue : qfalse;
}

static void RP_Lua_CmdCount( int clientNum )
{
	rpLuaCmdRing[clientNum][rpLuaCmdHead[clientNum]] = level.time + 1;
	rpLuaCmdHead[clientNum] = ( rpLuaCmdHead[clientNum] + 1 ) % RP_LUA_CMDS_PER_5S;
}

// GlobalUse: a chain a script call started stops at RP_LUA_USE_CHAIN uses (counted, logged once a minute)
qboolean RP_Lua_ScriptChainRoom( int chain )
{
	if ( !rp_luaInCall || chain < RP_LUA_USE_CHAIN )
		return qtrue;
	if ( rpLuaCurrent )
		rpLuaCurrent->chainsCut++;
	if ( !rpLuaChainLogged || level.time + 1 - rpLuaChainLogged >= 60000 || level.time + 1 < rpLuaChainLogged )
	{
		rpLuaChainLogged = level.time + 1;
		G_LogPrintf( "[lua:%s] a use chain stopped at %d uses: script budget\n", rpLuaCurrent ? rpLuaCurrent->name : "?", RP_LUA_USE_CHAIN );
	}
	return qfalse;
}

// a client the budget looks after: connected and not a bot (a bot has no command queue to overflow)
static qboolean RP_Lua_CmdCounted( int clientNum )
{
	const gentity_t *ent = &g_entities[clientNum];

	return ( ent->client && ent->client->pers.connected != CON_DISCONNECTED && !( ent->r.svFlags & SVF_BOT ) ) ? qtrue : qfalse;
}

static void RP_Lua_CmdHeld( int clientNum )
{
	if ( rpLuaCurrent )
		rpLuaCurrent->cmdsHeld++;
	if ( !rpLuaCmdLogged[clientNum] || level.time + 1 - rpLuaCmdLogged[clientNum] >= 1000 || level.time + 1 < rpLuaCmdLogged[clientNum] )
	{
		rpLuaCmdLogged[clientNum] = level.time + 1;
		G_LogPrintf( "[lua:%s] commands to %s^7 held back: script budget (%d a second)\n", rpLuaCurrent ? rpLuaCurrent->name : "?",
			g_entities[clientNum].client ? g_entities[clientNum].client->pers.netname : "?", RP_LUA_CMDS_PER_SEC );
	}
}

qboolean RP_Lua_ScriptCmdRoom( int clientNum, int n )
{
	if ( !rp_luaInCall || clientNum < 0 || clientNum >= MAX_CLIENTS || !RP_Lua_CmdCounted( clientNum ) )
		return qtrue;
	return RP_Lua_CmdFits( clientNum, n );
}

// something that reaches every client at once and cannot be split (a configstring change: the engine sends a "cs"
// reliable command to everyone): room for n on every counted client, all or nothing, and counted when it passes
qboolean RP_Lua_ScriptBroadcastRoom( int n )
{
	int i, k;

	if ( !rp_luaInCall )
		return qtrue;
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( RP_Lua_CmdCounted( i ) && !RP_Lua_CmdFits( i, n ) )
		{
			RP_Lua_CmdHeld( i );
			return qfalse;
		}
	}
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( RP_Lua_CmdCounted( i ) )
		{
			for ( k = 0; k < n; k++ )
				RP_Lua_CmdCount( i );
		}
	}
	return qtrue;
}

// trap->SendServerCommand: the engine's, counted per client while a script call is on the stack
static void RP_Lua_SendGuarded( int clientNum, const char *text )
{
	int i;

	if ( !rp_luaInCall )
	{
		rpLuaEngineSend( clientNum, text );
		return;
	}
	if ( clientNum >= 0 )
	{
		if ( clientNum < MAX_CLIENTS && RP_Lua_CmdCounted( clientNum ) )
		{
			if ( !RP_Lua_CmdFits( clientNum, 1 ) )
			{
				RP_Lua_CmdHeld( clientNum );
				return;
			}
			RP_Lua_CmdCount( clientNum );
		}
		rpLuaEngineSend( clientNum, text );
		return;
	}
	// everyone: sent once when everyone has room, else one by one to those who have
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( RP_Lua_CmdCounted( i ) && !RP_Lua_CmdFits( i, 1 ) )
			break;
	}
	if ( i == level.maxclients )
	{
		for ( i = 0; i < level.maxclients; i++ )
		{
			if ( RP_Lua_CmdCounted( i ) )
				RP_Lua_CmdCount( i );
		}
		rpLuaEngineSend( clientNum, text );
		return;
	}
	for ( i = 0; i < level.maxclients; i++ )
	{
		const gentity_t *ent = &g_entities[i];

		if ( !ent->client || ent->client->pers.connected == CON_DISCONNECTED )
			continue;
		if ( RP_Lua_CmdCounted( i ) )
		{
			if ( !RP_Lua_CmdFits( i, 1 ) )
			{
				RP_Lua_CmdHeld( i );
				continue;
			}
			RP_Lua_CmdCount( i );
		}
		rpLuaEngineSend( i, text );
	}
}

// from G_InitGame, for both entry paths (GetModuleAPI and dllEntry set trap before it): the game module uses a
// copy of the import table whose SendServerCommand is guarded; every other entry is the engine's own
void RP_Lua_TrapGuardInstall( void )
{
	if ( !trap || trap == &rpLuaTrap )
		return;
	rpLuaTrap = *trap;
	rpLuaEngineSend = trap->SendServerCommand;
	rpLuaTrap.SendServerCommand = RP_Lua_SendGuarded;
	trap = &rpLuaTrap;
}

// a database write (a payout) from inside a script call: RP_LUA_DB_WRITES_FRAME a server frame, all plugins together
qboolean RP_Lua_ScriptDbWrite( void )
{
	if ( !rp_luaInCall )
		return qtrue;
	if ( rpLuaDbFrame != level.time + 1 )
	{
		rpLuaDbFrame = level.time + 1;
		rpLuaDbWrites = 0;
	}
	if ( rpLuaDbWrites >= RP_LUA_DB_WRITES_FRAME )
	{
		if ( rpLuaCurrent )
			rpLuaCurrent->dbRefused++;
		if ( rpLuaDbLogged != rpLuaDbFrame )
		{
			rpLuaDbLogged = rpLuaDbFrame;
			G_LogPrintf( "[lua:%s] payout skipped: %d database writes from scripts this frame\n", rpLuaCurrent ? rpLuaCurrent->name : "?",
				RP_LUA_DB_WRITES_FRAME );
		}
		return qfalse;
	}
	rpLuaDbWrites++;
	return qtrue;
}

// GLX_U: [Lua] a script's text made fit for one engine command argument, in place: '"' would end the argument
// (the player's game shows the part before it), so it becomes '\''; a line break becomes a space. Script texts a
// player or an admin is shown pass here, or through RP_LuaAdmin_Notice's own loop (tests/textlint checks it).
static void RP_LuaApi_Clean( char *s )
{
	int i;

	for ( i = 0; s[i]; i++ )
	{
		if ( s[i] == '"' )
			s[i] = '\'';
		else if ( s[i] == '\n' || s[i] == '\r' )
			s[i] = ' ';
	}
}

// the text as one engine command argument: at most RP_LUA_TEXT_MAX bytes, no '"' (it would end the argument)
static const char *RP_LuaApi_Text( lua_State *L, int idx, char *out, int outSize )
{
	size_t len;
	const char *s = luaL_checklstring( L, idx, &len );

	if ( len > RP_LUA_TEXT_MAX )
		luaL_error( L, "text longer than %d bytes", RP_LUA_TEXT_MAX );
	Q_strncpyz( out, s, outSize );
	RP_LuaApi_Clean( out );
	return out;
}

static int RP_LuaApi_Refuse( lua_State *L, const char *why )
{
	lua_pushboolean( L, 0 );
	lua_pushstring( L, why );
	return 2;
}

static int RP_LuaApi_SendOne( lua_State *L, const char *cmd )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	int clientNum;
	char text[RP_LUA_TEXT_MAX + 1];

	RP_LuaApi_PlayerArg( L, 1, &clientNum );
	RP_LuaApi_Text( L, 2, text, sizeof( text ) );
	if ( !RP_LuaApi_SendRoom( clientNum ) )
	{
		p->refusedRecipients++;
		return RP_LuaApi_Refuse( L, "player busy" );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaApi_Refuse( L, "too fast" );
	trap->SendServerCommand( clientNum, va( "%s \"%s\n\"", cmd, text ) );
	RP_LuaApi_Sent( clientNum );
	p->sent++;
	lua_pushboolean( L, 1 );
	return 1;
}

static int RP_LuaApi_MsgPrint( lua_State *L )	{ return RP_LuaApi_SendOne( L, "print" ); }
static int RP_LuaApi_MsgCenter( lua_State *L )	{ return RP_LuaApi_SendOne( L, "cp" ); }

static int RP_LuaApi_MsgBroadcast( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	char text[RP_LUA_TEXT_MAX + 1];
	int i, recipients = 0, tokens;

	RP_LuaApi_Text( L, 1, text, sizeof( text ) );
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( RP_LuaApi_Counted( &g_entities[i] ) && RP_LuaApi_SendRoom( i ) )
			recipients++;
	}
	if ( !recipients )
	{
		lua_pushboolean( L, 1 );	// nobody to tell is not a refusal
		return 1;
	}
	tokens = ( recipients + RP_LUA_BROADCAST_PER_TOKEN - 1 ) / RP_LUA_BROADCAST_PER_TOKEN;
	if ( !RP_LuaApi_TakeToken( p, tokens ) )
		return RP_LuaApi_Refuse( L, "too fast" );
	for ( i = 0; i < level.maxclients; i++ )
	{
		if ( !RP_LuaApi_Counted( &g_entities[i] ) || !RP_LuaApi_SendRoom( i ) )
			continue;
		trap->SendServerCommand( i, va( "print \"%s\n\"", text ) );
		RP_LuaApi_Sent( i );
		p->sent++;
	}
	lua_pushboolean( L, 1 );
	return 1;
}

/*
==================
Events
==================
*/
static int RP_LuaApi_EventsOn( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *name = luaL_checkstring( L, 1 );
	int ev;

	luaL_checktype( L, 2, LUA_TFUNCTION );
	for ( ev = 0; ev < RPLE_COUNT; ev++ )
	{
		if ( !strcmp( rpLuaEventNames[ev], name ) )
			break;
	}
	if ( ev == RPLE_COUNT )
		return luaL_error( L, "no event named '%s'", name );
	if ( p->handlers[ev] != LUA_NOREF )
		luaL_unref( L, LUA_REGISTRYINDEX, p->handlers[ev] );
	lua_pushvalue( L, 2 );
	p->handlers[ev] = luaL_ref( L, LUA_REGISTRYINDEX );
	if ( ev == RPLE_FRAME )
		p->frameDue = level.time + RP_LUA_EVENT_FRAME_MS;
	return 0;
}

/*
==================
Commands
==================
*/
static int RP_LuaApi_CommandsAdd( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *name = luaL_checkstring( L, 1 );
	const char *help = luaL_optstring( L, 3, "" );
	int i;

	luaL_checktype( L, 2, LUA_TFUNCTION );
	if ( strlen( name ) < 3 || strlen( name ) > RP_LUA_CMD_NAME_MAX )
		return luaL_error( L, "a command name is 3 to %d characters", RP_LUA_CMD_NAME_MAX );
	for ( i = 0; name[i]; i++ )
	{
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
			return luaL_error( L, "a command name is letters, digits and '_'" );
	}
	if ( RP_IsBuiltInCommand( name ) )
		return RP_LuaApi_Refuse( L, "a built-in command has that name" );
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *o = &rpLuaPlugins[i];
		int k;

		if ( !o->inUse || !o->L )
			continue;
		for ( k = 0; k < o->numCommands; k++ )
		{
			if ( !Q_stricmp( o->cmdNames[k], name ) )
			{
				if ( o == p )
				{ // the plugin's own, replaced
					luaL_unref( L, LUA_REGISTRYINDEX, p->cmdRefs[k] );
					lua_pushvalue( L, 2 );
					p->cmdRefs[k] = luaL_ref( L, LUA_REGISTRYINDEX );
					Q_strncpyz( p->cmdHelp[k], help, sizeof( p->cmdHelp[k] ) );
					RP_LuaApi_Clean( p->cmdHelp[k] );	// GLX_U fix: [Lua] shown in /scripts help inside a quoted print
					lua_pushboolean( L, 1 );
					return 1;
				}
				return RP_LuaApi_Refuse( L, va( "plugin %s has that command", o->name ) );
			}
		}
	}
	if ( p->numCommands >= RP_LUA_MAX_COMMANDS )
		return RP_LuaApi_Refuse( L, "too many commands" );
	Q_strncpyz( p->cmdNames[p->numCommands], name, sizeof( p->cmdNames[0] ) );
	Q_strncpyz( p->cmdHelp[p->numCommands], help, sizeof( p->cmdHelp[0] ) );
	RP_LuaApi_Clean( p->cmdHelp[p->numCommands] );	// GLX_U fix: [Lua] a '"' cut its /scripts help line short
	lua_pushvalue( L, 2 );
	p->cmdRefs[p->numCommands] = luaL_ref( L, LUA_REGISTRYINDEX );
	p->numCommands++;
	lua_pushboolean( L, 1 );
	return 1;
}

/*
==================
Timers
==================
*/
static int RP_LuaApi_TimerAdd( lua_State *L, qboolean repeat )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	int ms = (int)luaL_checkinteger( L, 1 );
	rpLuaTimer_t *t;

	luaL_checktype( L, 2, LUA_TFUNCTION );
	if ( ms < RP_LUA_TIMER_MIN_MS )
		ms = RP_LUA_TIMER_MIN_MS;
	if ( p->numTimers >= RP_LUA_MAX_TIMERS )
		return RP_LuaApi_Refuse( L, "too many timers" );
	t = &p->timers[p->numTimers++];
	t->id = p->nextTimerId++;
	t->due = level.time + ms;
	t->interval = repeat ? ms : 0;
	lua_pushvalue( L, 2 );
	t->ref = luaL_ref( L, LUA_REGISTRYINDEX );
	lua_pushinteger( L, t->id );
	return 1;
}

static int RP_LuaApi_TimerAfter( lua_State *L )	{ return RP_LuaApi_TimerAdd( L, qfalse ); }
static int RP_LuaApi_TimerEvery( lua_State *L )	{ return RP_LuaApi_TimerAdd( L, qtrue ); }

static int RP_LuaApi_TimerCancel( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	int id = (int)luaL_checkinteger( L, 1 ), i;

	for ( i = 0; i < p->numTimers; i++ )
	{
		if ( p->timers[i].id == id && p->timers[i].ref != LUA_NOREF )
		{
			luaL_unref( L, LUA_REGISTRYINDEX, p->timers[i].ref );
			p->timers[i].ref = LUA_NOREF;	// compacted by RP_LuaApi_RunTimers
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}

void RP_LuaApi_RunTimers( rpLuaPlugin_t *p )
{
	int i, n = p->numTimers, kept;
	lua_State *L = p->L;

	// the frame event, a timer of its own
	if ( p->handlers[RPLE_FRAME] != LUA_NOREF && level.time >= p->frameDue )
	{
		if ( p->frameMsLeft > 0 && rpLuaFrameMsLeft > 0 )
		{
			p->frameDue = level.time + RP_LUA_EVENT_FRAME_MS;
			RP_Lua_CallRef( p, p->handlers[RPLE_FRAME], RP_LUA_CALL_MS, RP_Lua_PushEmptyEvent, NULL );
		}
		else
			p->deferredTimers++;
	}

	for ( i = 0; i < n; i++ )	// timers added during a call (n onwards) wait for the next frame
	{
		rpLuaTimer_t *t = &p->timers[i];

		if ( t->ref == LUA_NOREF || level.time < t->due )
			continue;
		if ( p->frameMsLeft <= 0 || rpLuaFrameMsLeft <= 0 )
		{
			p->deferredTimers++;
			break;	// stays due; runs next frame
		}
		if ( t->interval )
		{
			t->due += t->interval;
			if ( t->due < level.time )
				t->due = level.time + t->interval;	// fell behind: no catching up
		}
		if ( t->interval )
			RP_Lua_CallRef( p, t->ref, RP_LUA_CALL_MS, RP_Lua_PushEmptyEvent, NULL );
		else
		{ // once: the ref is dropped first, so a Timer.cancel from inside finds it gone
			int ref = t->ref;

			t->ref = LUA_NOREF;
			RP_Lua_CallRef( p, ref, RP_LUA_CALL_MS, RP_Lua_PushEmptyEvent, NULL );
			luaL_unref( L, LUA_REGISTRYINDEX, ref );
		}
		if ( !p->L )
			return;	// cannot happen in P1 (closes are queued), kept as the rule
	}

	for ( i = 0, kept = 0; i < p->numTimers; i++ )
	{
		if ( p->timers[i].ref != LUA_NOREF )
			p->timers[kept++] = p->timers[i];
	}
	p->numTimers = kept;
}

/*
==================
World
==================
*/
static int RP_LuaApi_WorldPlayersWithin( lua_State *L )
{
	vec3_t origin;
	float radius = (float)luaL_checknumber( L, 2 ), r2;
	int i, n = 0;

	luaL_checktype( L, 1, LUA_TTABLE );
	lua_getfield( L, 1, "x" ); origin[0] = (float)luaL_checknumber( L, -1 ); lua_pop( L, 1 );
	lua_getfield( L, 1, "y" ); origin[1] = (float)luaL_checknumber( L, -1 ); lua_pop( L, 1 );
	lua_getfield( L, 1, "z" ); origin[2] = (float)luaL_checknumber( L, -1 ); lua_pop( L, 1 );
	if ( radius < 0 )
		radius = 0;
	if ( radius > RP_LUA_RADIUS_MAX )
		radius = RP_LUA_RADIUS_MAX;
	r2 = radius * radius;

	lua_newtable( L );
	for ( i = 0; i < level.maxclients; i++ )
	{
		gentity_t *ent = &g_entities[i];
		vec3_t d;

		if ( !RP_LuaApi_Counted( ent ) || ent->client->sess.sessionTeam == TEAM_SPECTATOR )
			continue;
		VectorSubtract( ent->client->ps.origin, origin, d );
		if ( DotProduct( d, d ) > r2 )
			continue;
		RP_LuaApi_PushPlayer( L, i );
		lua_rawseti( L, -2, ++n );
	}
	return 1;
}

static int RP_LuaApi_WorldTime( lua_State *L )
{
	lua_pushinteger( L, level.time );
	return 1;
}

static int RP_LuaApi_WorldMapname( lua_State *L )
{
	lua_pushstring( L, level.rawmapname );
	return 1;
}

static int RP_LuaApi_WorldEntity( lua_State *L )
{
	const char *targetname = luaL_checkstring( L, 1 );
	gentity_t *ent = G_Find( NULL, FOFS( targetname ), (char *)targetname );

	if ( !ent )
	{
		lua_pushnil( L );
		return 1;
	}
	lua_createtable( L, 0, 3 );
	lua_pushinteger( L, ent - g_entities );
	lua_setfield( L, -2, "number" );
	lua_pushstring( L, ent->classname ? ent->classname : "" );
	lua_setfield( L, -2, "classname" );
	lua_createtable( L, 0, 3 );
	lua_pushnumber( L, ent->r.currentOrigin[0] ); lua_setfield( L, -2, "x" );
	lua_pushnumber( L, ent->r.currentOrigin[1] ); lua_setfield( L, -2, "y" );
	lua_pushnumber( L, ent->r.currentOrigin[2] ); lua_setfield( L, -2, "z" );
	lua_setfield( L, -2, "origin" );
	return 1;
}

/*
==================
World, read-only sight and search (GLX_U: [Lua] P3 trace/find)

World.trace, World.visible, World.contents and World.find read the world and change nothing: no hook,
nothing owned, no command token. The engine is only ever handed finite coordinates within
RP_LUA_COORD_MAX, and a trace is a C step the count hook never sees, so a call may make at most
RP_LUA_TRACES_PER_CALL of them (the 10 ms clock and the overrun check stay behind that).
==================
*/
#define RP_LUA_FIND_SPEC_MAX	64	// a classname or targetname in a World.find spec

// a vector {x, y, z} of finite coordinates within the world, else an error naming the field
static void RP_LuaApi_ReadCoord( lua_State *L, int idx, const char *what, vec3_t out )
{
	static const char *const axis[3] = { "x", "y", "z" };
	int i;

	if ( idx < 0 )
		idx = lua_gettop( L ) + 1 + idx;
	if ( lua_type( L, idx ) != LUA_TTABLE )
		luaL_error( L, "%s must be a vector {x, y, z}", what );
	for ( i = 0; i < 3; i++ )
	{
		lua_Number v;

		lua_getfield( L, idx, axis[i] );
		if ( lua_type( L, -1 ) != LUA_TNUMBER )
			luaL_error( L, "%s.%s must be a number", what, axis[i] );
		v = lua_tonumber( L, -1 );
		lua_pop( L, 1 );
		if ( v != v || v > RP_LUA_COORD_MAX || v < -RP_LUA_COORD_MAX )
			luaL_error( L, "%s.%s is not a finite coordinate within %d", what, axis[i], (int)RP_LUA_COORD_MAX );
		out[i] = (float)v;
	}
}

static void RP_LuaApi_PushVec( lua_State *L, const vec3_t v )
{
	lua_createtable( L, 0, 3 );
	lua_pushnumber( L, v[0] ); lua_setfield( L, -2, "x" );
	lua_pushnumber( L, v[1] ); lua_setfield( L, -2, "y" );
	lua_pushnumber( L, v[2] ); lua_setfield( L, -2, "z" );
}

// a plain table describing an entity (never a handle: the map is read, not held)
static void RP_LuaApi_PushEntityInfo( lua_State *L, const gentity_t *ent )
{
	lua_createtable( L, 0, 6 );
	lua_pushinteger( L, ent - g_entities );
	lua_setfield( L, -2, "number" );
	lua_pushstring( L, ( ent - g_entities == ENTITYNUM_WORLD ) ? "worldspawn" : ( ent->classname ? ent->classname : "" ) );
	lua_setfield( L, -2, "classname" );
	if ( ent->targetname && ent->targetname[0] )
	{
		lua_pushstring( L, ent->targetname );
		lua_setfield( L, -2, "targetname" );
	}
	if ( ent->client && ent->s.number >= MAX_CLIENTS )
	{
		lua_pushstring( L, ent->NPC_type ? ent->NPC_type : "" );
		lua_setfield( L, -2, "npc_type" );
	}
	RP_LuaApi_PushVec( L, ent->r.currentOrigin );
	lua_setfield( L, -2, "origin" );
	if ( rpLuaCurrent && ent->rpLuaPlugin == (int)( rpLuaCurrent - rpLuaPlugins ) + 1 )
	{
		lua_pushboolean( L, 1 );
		lua_setfield( L, -2, "mine" );
	}
}

// what a trace hit: a player handle, an entity table, or nil
static void RP_LuaApi_PushHit( lua_State *L, int entityNum )
{
	if ( entityNum < 0 || entityNum >= ENTITYNUM_NONE )
		lua_pushnil( L );
	else if ( entityNum < MAX_CLIENTS && RP_LuaApi_Counted( &g_entities[entityNum] ) )
		RP_LuaApi_PushPlayer( L, entityNum );
	else
		RP_LuaApi_PushEntityInfo( L, &g_entities[entityNum] );
}

// a point for trace/visible: a vector, a player handle (eye height) or one of the plugin's own entities;
// *ent is set to the entity for the latter two (its number passes through the trace)
static void RP_LuaApi_ReadPoint( lua_State *L, int idx, const char *what, vec3_t out, gentity_t **ent )
{
	gentity_t *e;

	*ent = NULL;
	if ( luaL_testudata( L, idx, RP_LUA_PLAYER_MT ) )
	{
		e = RP_LuaApi_Handle( L, idx, qtrue );
		VectorCopy( e->client->ps.origin, out );
		out[2] += e->client->ps.viewheight;
		*ent = e;
		return;
	}
	if ( ( e = RP_LuaWorld_HandleAt( L, idx ) ) != NULL )
	{
		VectorCopy( e->r.currentOrigin, out );
		*ent = e;
		return;
	}
	RP_LuaApi_ReadCoord( L, idx, what, out );
}

static int RP_LuaApi_MaskByName( lua_State *L, const char *name )
{
	if ( !name || !strcmp( name, "opaque" ) ) return MASK_OPAQUE;
	if ( !strcmp( name, "solid" ) ) return MASK_SOLID;
	if ( !strcmp( name, "shot" ) ) return MASK_SHOT;
	if ( !strcmp( name, "players" ) ) return CONTENTS_BODY;
	return luaL_error( L, "mask must be opaque, solid, shot or players" );
}

// the trace itself; qfalse (and false, "why" pushed) when the call has made its share
static qboolean RP_LuaApi_DoTrace( lua_State *L, trace_t *tr, const vec3_t from, const vec3_t to, int passEnt, int mask, float size )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );

	if ( p->tracesCall >= RP_LUA_TRACES_PER_CALL )
	{
		lua_pushboolean( L, 0 );
		lua_pushstring( L, "too many traces in one call" );
		return qfalse;
	}
	p->tracesCall++;
	p->traces++;
	memset( tr, 0, sizeof( *tr ) );
	if ( VectorCompare( from, to ) )
	{ // nowhere to go: answered here
		tr->fraction = 1.0f;
		tr->entityNum = ENTITYNUM_NONE;
		VectorCopy( to, tr->endpos );
		return qtrue;
	}
	if ( size > 0 )
	{
		vec3_t mins, maxs;

		VectorSet( mins, -size, -size, -size );
		VectorSet( maxs, size, size, size );
		trap->Trace( tr, from, mins, maxs, to, passEnt, mask, qfalse, 0, 0 );
	}
	else
		trap->Trace( tr, from, NULL, NULL, to, passEnt, mask, qfalse, 0, 0 );
	return qtrue;
}

// World.trace(from, to [, {mask=, ignore=, size=}])
static int RP_LuaApi_WorldTrace( lua_State *L )
{
	vec3_t from, to;
	trace_t tr;
	int passEnt = ENTITYNUM_NONE, mask = MASK_OPAQUE;
	float size = 0;

	RP_LuaApi_ReadCoord( L, 1, "from", from );
	RP_LuaApi_ReadCoord( L, 2, "to", to );
	if ( !lua_isnoneornil( L, 3 ) )
	{
		luaL_checktype( L, 3, LUA_TTABLE );
		lua_getfield( L, 3, "mask" );
		if ( !lua_isnil( L, -1 ) )
			mask = RP_LuaApi_MaskByName( L, luaL_checkstring( L, -1 ) );
		lua_pop( L, 1 );
		lua_getfield( L, 3, "ignore" );
		if ( !lua_isnil( L, -1 ) )
		{
			gentity_t *e = RP_LuaWorld_HandleAt( L, -1 );

			if ( !e )
			{
				if ( !luaL_testudata( L, -1, RP_LUA_PLAYER_MT ) )
					return luaL_error( L, "ignore must be a player or one of the plugin's own entities" );
				e = RP_LuaApi_Handle( L, -1, qtrue );
			}
			passEnt = e - g_entities;
		}
		lua_pop( L, 1 );
		lua_getfield( L, 3, "size" );
		if ( !lua_isnil( L, -1 ) )
		{
			lua_Number v = luaL_checknumber( L, -1 );

			if ( v != v || v < 0 || v > 64 )
				return luaL_error( L, "size must be 0 to 64" );
			size = (float)v;
		}
		lua_pop( L, 1 );
	}

	if ( !RP_LuaApi_DoTrace( L, &tr, from, to, passEnt, mask, size ) )
		return 2;

	lua_createtable( L, 0, 6 );
	lua_pushnumber( L, tr.fraction ); lua_setfield( L, -2, "fraction" );
	lua_pushboolean( L, tr.fraction < 1.0f ); lua_setfield( L, -2, "hit" );
	lua_pushboolean( L, tr.startsolid ); lua_setfield( L, -2, "startsolid" );
	RP_LuaApi_PushVec( L, tr.endpos ); lua_setfield( L, -2, "endpos" );
	if ( tr.fraction < 1.0f )
	{
		RP_LuaApi_PushVec( L, tr.plane.normal ); lua_setfield( L, -2, "normal" );
	}
	RP_LuaApi_PushHit( L, tr.entityNum ); lua_setfield( L, -2, "entity" );
	return 1;
}

// World.visible(a, b [, ignore]): nothing opaque between a and b (points, players at eye height, own entities)
static int RP_LuaApi_WorldVisible( lua_State *L )
{
	vec3_t a, b;
	gentity_t *ea, *eb, *ignore = NULL;
	trace_t tr;
	int passEnt;

	RP_LuaApi_ReadPoint( L, 1, "a", a, &ea );
	RP_LuaApi_ReadPoint( L, 2, "b", b, &eb );
	if ( !lua_isnoneornil( L, 3 ) )
	{
		ignore = RP_LuaWorld_HandleAt( L, 3 );
		if ( !ignore )
		{
			if ( !luaL_testudata( L, 3, RP_LUA_PLAYER_MT ) )
				return luaL_error( L, "ignore must be a player or one of the plugin's own entities" );
			ignore = RP_LuaApi_Handle( L, 3, qtrue );
		}
	}
	passEnt = ignore ? ignore - g_entities : ( ea ? ea - g_entities : ENTITYNUM_NONE );
	if ( !RP_LuaApi_DoTrace( L, &tr, a, b, passEnt, MASK_OPAQUE, 0 ) )
		return 2;
	// a trace to a body stops at it: reaching b's own entity counts as seeing it
	lua_pushboolean( L, tr.fraction >= 1.0f || ( eb && tr.entityNum == eb - g_entities ) );
	return 1;
}

// World.contents(pos): what is at a point, as booleans
static int RP_LuaApi_WorldContents( lua_State *L )
{
	vec3_t pos;
	int c;

	RP_LuaApi_ReadCoord( L, 1, "pos", pos );
	c = trap->PointContents( pos, ENTITYNUM_NONE );
	lua_createtable( L, 0, 7 );
	lua_pushboolean( L, ( c & CONTENTS_SOLID ) != 0 ); lua_setfield( L, -2, "solid" );
	lua_pushboolean( L, ( c & CONTENTS_WATER ) != 0 ); lua_setfield( L, -2, "water" );
	lua_pushboolean( L, ( c & CONTENTS_LAVA ) != 0 ); lua_setfield( L, -2, "lava" );
	lua_pushboolean( L, ( c & CONTENTS_SLIME ) != 0 ); lua_setfield( L, -2, "slime" );
	lua_pushboolean( L, ( c & CONTENTS_TRIGGER ) != 0 ); lua_setfield( L, -2, "trigger" );
	lua_pushboolean( L, ( c & CONTENTS_PLAYERCLIP ) != 0 ); lua_setfield( L, -2, "playerclip" );
	lua_pushboolean( L, ( c & ( CONTENTS_SOLID | CONTENTS_WATER | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_TRIGGER | CONTENTS_PLAYERCLIP ) ) == 0 );
	lua_setfield( L, -2, "empty" );
	return 1;
}

// a string field of the spec, up to RP_LUA_FIND_SPEC_MAX, or NULL
static const char *RP_LuaApi_FindString( lua_State *L, const char *key, char *buf, int size )
{
	const char *v;
	size_t len;

	lua_getfield( L, 1, key );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return NULL;
	}
	v = lua_tolstring( L, -1, &len );
	if ( !v || !len || len >= (size_t)size )
		luaL_error( L, "%s must be a string of 1 to %d characters", key, size - 1 );
	Q_strncpyz( buf, v, size );
	lua_pop( L, 1 );
	return buf;
}

// World.find{classname=, targetname=, near=, radius=, box={mins=,maxs=}, npcs=, limit=}: plain tables, nearest first with near
static int RP_LuaApi_WorldFind( lua_State *L )
{
	char classname[RP_LUA_FIND_SPEC_MAX], targetname[RP_LUA_FIND_SPEC_MAX];
	const char *cn, *tn;
	vec3_t near, mins, maxs;
	qboolean hasNear = qfalse, hasBox = qfalse;
	float r2 = 0;
	int npcs = -1, limit = RP_LUA_FIND_MAX, i, n = 0, region;
	struct { gentity_t *ent; float d2; } picks[RP_LUA_FIND_MAX];

	luaL_checktype( L, 1, LUA_TTABLE );
	lua_settop( L, 1 );
	cn = RP_LuaApi_FindString( L, "classname", classname, sizeof( classname ) );
	tn = RP_LuaApi_FindString( L, "targetname", targetname, sizeof( targetname ) );
	lua_getfield( L, 1, "near" );
	if ( !lua_isnil( L, -1 ) )
	{
		lua_Number radius;

		RP_LuaApi_ReadCoord( L, -1, "near", near );
		lua_getfield( L, 1, "radius" );
		radius = luaL_checknumber( L, -1 );
		lua_pop( L, 1 );
		if ( radius != radius || radius < 1 || radius > RP_LUA_RADIUS_MAX )
			return luaL_error( L, "radius must be 1 to %d", (int)RP_LUA_RADIUS_MAX );
		r2 = (float)( radius * radius );
		hasNear = qtrue;
	}
	lua_pop( L, 1 );
	lua_getfield( L, 1, "box" );
	if ( !lua_isnil( L, -1 ) )
	{
		luaL_checktype( L, -1, LUA_TTABLE );
		lua_getfield( L, -1, "mins" );
		RP_LuaApi_ReadCoord( L, -1, "box.mins", mins );
		lua_pop( L, 1 );
		lua_getfield( L, -1, "maxs" );
		RP_LuaApi_ReadCoord( L, -1, "box.maxs", maxs );
		lua_pop( L, 1 );
		for ( i = 0; i < 3; i++ )
		{
			if ( maxs[i] < mins[i] || maxs[i] - mins[i] > RP_LUA_RADIUS_MAX )
				return luaL_error( L, "box sides must be 0 to %d, maxs above mins", (int)RP_LUA_RADIUS_MAX );
		}
		hasBox = qtrue;
	}
	lua_pop( L, 1 );
	lua_getfield( L, 1, "npcs" );
	if ( !lua_isnil( L, -1 ) )
		npcs = lua_toboolean( L, -1 ) ? 1 : 0;
	lua_pop( L, 1 );
	lua_getfield( L, 1, "limit" );
	if ( !lua_isnil( L, -1 ) )
	{
		lua_Integer v = luaL_checkinteger( L, -1 );

		if ( v < 1 || v > RP_LUA_FIND_MAX )
			return luaL_error( L, "limit must be 1 to %d", RP_LUA_FIND_MAX );
		limit = (int)v;
	}
	lua_pop( L, 1 );
	if ( !cn && !tn && !hasNear && !hasBox )
		return luaL_error( L, "find needs a classname, a targetname, near or box" );

	// both regions, players and temp events never; the nearest kept when there is a near
	for ( region = 0; region < 2; region++ )
	{
		int first = region ? MAX_GENTITIES : MAX_CLIENTS;
		int end = region ? MAX_GENTITIES + level.num_logicalents : level.num_entities;

		for ( i = first; i < end; i++ )
		{
			gentity_t *ent = &g_entities[i];
			float d2 = 0;
			int k;

			if ( !ent->inuse || ent->s.eType >= ET_EVENTS || i == ENTITYNUM_WORLD || i == ENTITYNUM_NONE )
				continue;
			if ( npcs >= 0 && ( ent->client != NULL ) != ( npcs == 1 ) )
				continue;
			if ( cn && ( !ent->classname || Q_stricmp( ent->classname, cn ) ) )
				continue;
			if ( tn && ( !ent->targetname || Q_stricmp( ent->targetname, tn ) ) )
				continue;
			if ( hasBox )
			{
				for ( k = 0; k < 3; k++ )
					if ( ent->r.currentOrigin[k] < mins[k] || ent->r.currentOrigin[k] > maxs[k] )
						break;
				if ( k < 3 )
					continue;
			}
			if ( hasNear )
			{
				vec3_t d;

				VectorSubtract( ent->r.currentOrigin, near, d );
				d2 = DotProduct( d, d );
				if ( d2 > r2 )
					continue;
				// keep the picks sorted by distance; the farthest falls off the end
				for ( k = n; k > 0 && picks[k - 1].d2 > d2; k-- )
				{
					if ( k < limit )
						picks[k] = picks[k - 1];
				}
				if ( k >= limit )
					continue;
				picks[k].ent = ent;
				picks[k].d2 = d2;
				if ( n < limit )
					n++;
				continue;
			}
			picks[n].ent = ent;
			picks[n].d2 = 0;
			if ( ++n >= limit )
				break;
		}
		if ( !hasNear && n >= limit )
			break;
	}

	lua_createtable( L, n, 0 );
	for ( i = 0; i < n; i++ )
	{
		RP_LuaApi_PushEntityInfo( L, picks[i].ent );
		if ( hasNear )
		{
			lua_pushnumber( L, sqrt( picks[i].d2 ) );
			lua_setfield( L, -2, "distance" );
		}
		lua_rawseti( L, -2, i + 1 );
	}
	return 1;
}

static int RP_LuaApi_WorldWatch( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	size_t len;
	const char *name = luaL_checklstring( L, 1, &len );
	int i;

	if ( !len || len >= MAX_QPATH )
		return luaL_error( L, "a targetname is 1 to %d characters", MAX_QPATH - 1 );
	for ( i = 0; i < p->numWatches; i++ )
	{
		if ( !Q_stricmp( p->watches[i], name ) )
		{
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	if ( p->numWatches >= RP_LUA_MAX_WATCHES )
		return RP_LuaApi_Refuse( L, "too many watched targetnames" );
	if ( rp_luaWatchCount >= RP_LUA_MAX_WATCHES_ALL )
		return RP_LuaApi_Refuse( L, "too many watched targetnames on the server" );
	Q_strncpyz( p->watches[p->numWatches++], name, MAX_QPATH );
	rp_luaWatchCount++;
	lua_pushboolean( L, 1 );
	return 1;
}

static int RP_LuaApi_WorldUnwatch( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *name = luaL_checkstring( L, 1 );
	int i;

	for ( i = 0; i < p->numWatches; i++ )
	{
		if ( !Q_stricmp( p->watches[i], name ) )
		{
			p->numWatches--;
			if ( i < p->numWatches )
				memcpy( p->watches[i], p->watches[p->numWatches], MAX_QPATH );
			rp_luaWatchCount--;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	lua_pushboolean( L, 0 );
	return 1;
}

/*
==================
Cvar, Log
==================
*/
static int RP_LuaApi_CvarGet( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	char value[MAX_CVAR_VALUE_STRING];

	if ( Q_stricmpn( name, "rp_", 3 ) && Q_stricmpn( name, "g_", 2 ) && Q_stricmpn( name, "sv_", 3 ) )
		return luaL_error( L, "only rp_*, g_* and sv_* cvars can be read" );
	trap->Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	lua_pushstring( L, value );
	return 1;
}

static int RP_LuaApi_LogInfo( lua_State *L )
{
	char text[RP_LUA_TEXT_MAX + 1];

	RP_LuaApi_Text( L, 1, text, sizeof( text ) );
	G_LogPrintf( "[lua:%s] %s\n", RP_LUA_PLUGIN( L )->name, text );
	return 0;
}

/*
==================
Opening
==================
*/
static const luaL_Reg rpLuaPlayerMeta[] = {
	{ "__index",	RP_LuaApi_PlayerIndex },
	{ "__eq",		RP_LuaApi_PlayerEq },
	{ "__tostring",	RP_LuaApi_PlayerToString },
	{ NULL, NULL }
};
static const luaL_Reg rpLuaEvents[] = { { "on", RP_LuaApi_EventsOn }, { NULL, NULL } };
static const luaL_Reg rpLuaMsg[] = {
	{ "print", RP_LuaApi_MsgPrint }, { "center", RP_LuaApi_MsgCenter }, { "broadcast", RP_LuaApi_MsgBroadcast }, { NULL, NULL }
};
static const luaL_Reg rpLuaCommands[] = { { "add", RP_LuaApi_CommandsAdd }, { NULL, NULL } };
static const luaL_Reg rpLuaTimer[] = {
	{ "after", RP_LuaApi_TimerAfter }, { "every", RP_LuaApi_TimerEvery }, { "cancel", RP_LuaApi_TimerCancel }, { NULL, NULL }
};
static const luaL_Reg rpLuaPlayers[] = { { "all", RP_LuaApi_PlayersAll }, { "get", RP_LuaApi_PlayersGet }, { NULL, NULL } };
static const luaL_Reg rpLuaWorld[] = {
	{ "players_within", RP_LuaApi_WorldPlayersWithin }, { "time", RP_LuaApi_WorldTime }, { "mapname", RP_LuaApi_WorldMapname },
	{ "entity", RP_LuaApi_WorldEntity }, { "watch", RP_LuaApi_WorldWatch }, { "unwatch", RP_LuaApi_WorldUnwatch },
	{ "trace", RP_LuaApi_WorldTrace }, { "visible", RP_LuaApi_WorldVisible }, { "contents", RP_LuaApi_WorldContents }, { "find", RP_LuaApi_WorldFind }, { NULL, NULL }
};
static const luaL_Reg rpLuaCvar[] = { { "get", RP_LuaApi_CvarGet }, { NULL, NULL } };
static const luaL_Reg rpLuaLog[] = { { "info", RP_LuaApi_LogInfo }, { NULL, NULL } };

static void RP_LuaApi_Lib( lua_State *L, const char *name, const luaL_Reg *funcs )
{
	lua_newtable( L );
	luaL_setfuncs( L, funcs, 0 );
	lua_setglobal( L, name );
}

void RP_LuaApi_Open( rpLuaPlugin_t *p )
{
	lua_State *L = p->L;

	luaL_newmetatable( L, RP_LUA_PLAYER_MT );
	luaL_setfuncs( L, rpLuaPlayerMeta, 0 );
	lua_pushboolean( L, 0 );
	lua_setfield( L, -2, "__metatable" );
	lua_pop( L, 1 );

	RP_LuaApi_Lib( L, "Events", rpLuaEvents );
	RP_LuaApi_Lib( L, "Msg", rpLuaMsg );
	RP_LuaApi_Lib( L, "Commands", rpLuaCommands );
	RP_LuaApi_Lib( L, "Timer", rpLuaTimer );
	RP_LuaApi_Lib( L, "Players", rpLuaPlayers );
	RP_LuaApi_Lib( L, "World", rpLuaWorld );
	RP_LuaApi_Lib( L, "Cvar", rpLuaCvar );
	RP_LuaApi_Lib( L, "Log", rpLuaLog );
	lua_pushstring( L, p->name );
	lua_setglobal( L, "PLUGIN_NAME" );
	lua_pushinteger( L, 1 );
	lua_setglobal( L, "API_VERSION" );
}

void RP_LuaApi_Reset( void )
{ // a new map: level.time starts over, so the old timestamps must go
	memset( rpLuaSends, 0, sizeof( rpLuaSends ) );
	memset( rpLuaCmdRing, 0, sizeof( rpLuaCmdRing ) );
	memset( rpLuaCmdHead, 0, sizeof( rpLuaCmdHead ) );
	rpLuaChainLogged = 0;
	memset( rpLuaCmdLogged, 0, sizeof( rpLuaCmdLogged ) );
	rpLuaDbFrame = rpLuaDbWrites = rpLuaDbLogged = 0;
}

void RP_LuaApi_ClosePlugin( rpLuaPlugin_t *p )
{ // the state is about to close; only what lives outside it needs undoing
	rp_luaWatchCount -= p->numWatches;
	p->numWatches = 0;
	p->numCommands = 0;
	p->numTimers = 0;
}
