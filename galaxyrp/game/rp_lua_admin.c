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
			RP_LuaAdmin_Out( ent, va( "   world: %d props, %d NPCs and %d vehicles alive (%d, %d and %d spawned this map, %d refused); %d uses, %d effects, %d sounds\n",
				RP_LuaWorld_CountLive( p, qfalse ), RP_LuaWorld_CountLive( p, qtrue ), RP_LuaWorld_CountVehicles( p ), p->propSpawns, p->npcSpawns, p->vehicleSpawns,
				p->spawnsRefused, p->uses, p->effects, p->sounds ) );
		if ( p->L && ( p->usesBusy || p->cmdsHeld || p->dbRefused ) )
			RP_LuaAdmin_Out( ent, va( "   guards: %d uses refused (player busy), %d commands held back, %d payouts skipped (database writes)\n",
				p->usesBusy, p->cmdsHeld, p->dbRefused ) );
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
	{
		rpLuaPlugin_t *p = RP_Lua_Find( arg );

		if ( p && p->L )
			RP_LuaAdmin_Out( ent, va( "%s is already running.\n", p->name ) );
		else if ( !RP_Lua_QueueLoad( arg ) )
			RP_LuaAdmin_Out( ent, "That is not a plugin name (letters, digits, '_' and '-', up to 32).\n" );
		else
			RP_LuaAdmin_Out( ent, va( "Loading %s at the end of this frame; see the log if it does not appear in /scripts list.\n", arg ) );
	}
	else if ( !Q_stricmp( sub, "unload" ) || !Q_stricmp( sub, "disable" ) )
	{
		rpLuaOp_t op = Q_stricmp( sub, "unload" ) ? RPLOP_DISABLE : RPLOP_UNLOAD;
		rpLuaPlugin_t *p = RP_Lua_Find( arg );

		if ( p && !p->L && op == RPLOP_UNLOAD )
		{ // switched off: nothing to close, the slot goes
			memset( p, 0, sizeof( *p ) );
			RP_LuaAdmin_Out( ent, va( "%s unloaded.\n", arg ) );
		}
		else if ( !RP_Lua_QueueOp( arg, op ) )
			RP_LuaAdmin_Out( ent, va( "%s is not running.\n", arg ) );
		else
			RP_LuaAdmin_Out( ent, va( "%s will be %s at the end of this frame.\n", arg, op == RPLOP_UNLOAD ? "unloaded" : "switched off" ) );
	}
	else if ( !Q_stricmp( sub, "reload" ) )
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
		RP_LuaAdmin_Out( ent, n ? va( "%d plugin%s reloading at the end of this frame.\n", n, n == 1 ? "" : "s" ) : va( "%s is not loaded.\n", arg ) );
	}
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
