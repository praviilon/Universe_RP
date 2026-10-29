/*
===========================================================================
GLX_U: [Lua] server-side scripting -- what the game code sees.

Plugins (lua/plugins/<name>/plugin.lua in the mod folder, loose files only) run in a Lua state of their
own, told what happens through the events below, and able to answer with messages, commands, timers and
stored data (P1), and, as a player would, with uses of named entities, props and NPCs of their own, and
effects and sounds already registered (P2, rp_lua_world.c). The design (and why) is in the project's Lua
v2 plan. Every hook here is one compare when rp_lua is 0 or no plugin is loaded, and none of them changes
what the game does -- except RP_LUA_COMMAND, which answers a command no built-in has.
===========================================================================
*/
#pragma once

struct gentity_s;

extern int rp_luaActive;	// plugins loaded (0: every hook returns at once)

void RP_Lua_Init( void );
void RP_Lua_Shutdown( void );
void RP_Lua_Frame( void );
void RP_Lua_Generation( int clientNum );
void RP_Lua_EventBegin( struct gentity_s *ent );
void RP_Lua_EventDisconnect( struct gentity_s *ent );
void RP_Lua_EventChat( struct gentity_s *ent, struct gentity_s *target, int mode, const char *text );	// target: tells only
void RP_Lua_EventDeath( struct gentity_s *victim, struct gentity_s *attacker, int mod );
void RP_Lua_EventUsed( struct gentity_s *self, struct gentity_s *activator );
void RP_Lua_EventAccount( struct gentity_s *ent, const char *event, int number );	// login, logout, character_select, level_up
qboolean RP_Lua_Command( struct gentity_s *ent, const char *cmd );
void RP_Lua_CharacterRemoved( int charId );
void RP_Lua_EventDialog( struct gentity_s *player, const char *dialogName, int dialogNum, int choice, const char *option, const char *reason );	// g_dialog.c: an answer, or why it closed
void Cmd_Scripts_f( struct gentity_s *ent );
void SP_target_lua( struct gentity_s *self );	// rp_lua_admin.c: the map entity that loads, unloads or reloads a plugin (g_spawn.c spawns[])
void target_lua_use( struct gentity_s *self, struct gentity_s *other, struct gentity_s *activator );
qboolean RP_Lua_ValidName( const char *name );	// a plugin folder name: letters, digits, '_' and '-', up to 32
void Svcmd_Lua_f( void );
void Svcmd_LuaReload_f( void );
void Svcmd_LuaList_f( void );
int RP_AdminBitByTitle( const char *title );	// g_cmds.c: an admin power's bit by its /adminlist title, -1 for none
qboolean RP_IsBuiltInCommand( const char *name );	// g_cmds.c: the name is in commands[]
extern int rp_luaWatchCount;	// targetnames any plugin watches (RP_LUA_EVENT_USED's first test)
extern int rp_luaInCall;		// a script call is on the stack: the stability guards below apply
// the stability guards (rp_lua_api.c): only while a script call is on the stack; always qtrue outside one
void RP_Lua_TrapGuardInstall( void );	// G_InitGame: trap->SendServerCommand counted per client in script calls
qboolean RP_Lua_ScriptCmdRoom( int clientNum, int n );	// n more commands to that client fit this second
qboolean RP_Lua_ScriptBroadcastRoom( int n );	// n more to every client fit (a configstring change), counted when they do
qboolean RP_Lua_ScriptDbWrite( void );	// one more database write fits this frame (and is counted)
const char *RP_Lua_CurrentName( void );	// the plugin whose call runs, or NULL

#define RP_LUA_INIT()					do { if ( rp_lua.integer ) RP_Lua_Init(); } while ( 0 )
#define RP_LUA_SHUTDOWN()				do { if ( rp_lua.integer ) RP_Lua_Shutdown(); } while ( 0 )
#define RP_LUA_FRAME()					do { if ( rp_lua.integer ) RP_Lua_Frame(); } while ( 0 )
#define RP_LUA_GEN( n )					do { if ( rp_lua.integer ) RP_Lua_Generation( n ); } while ( 0 )
#define RP_LUA_EVENT_BEGIN( e )			do { if ( rp_luaActive ) RP_Lua_EventBegin( e ); } while ( 0 )
#define RP_LUA_EVENT_DISCONNECT( e )	do { if ( rp_luaActive ) RP_Lua_EventDisconnect( e ); } while ( 0 )
#define RP_LUA_EVENT_CHAT( e, tg, m, t )	do { if ( rp_luaActive ) RP_Lua_EventChat( e, tg, m, t ); } while ( 0 )
#define RP_LUA_EVENT_DEATH( v, a, m )	do { if ( rp_luaActive ) RP_Lua_EventDeath( v, a, m ); } while ( 0 )
#define RP_LUA_EVENT_USED( s, a )		do { if ( rp_luaWatchCount > 0 && (s)->targetname ) RP_Lua_EventUsed( s, a ); } while ( 0 )
#define RP_LUA_EVENT_ACCOUNT( e, n, x )	do { if ( rp_luaActive ) RP_Lua_EventAccount( e, n, x ); } while ( 0 )
#define RP_LUA_COMMAND( e, c )			( rp_luaActive ? RP_Lua_Command( e, c ) : qfalse )
#define RP_LUA_CHAR_REMOVED( id )		do { if ( rp_lua.integer ) RP_Lua_CharacterRemoved( id ); } while ( 0 )	// the store is open with or without a plugin running
#define RP_LUA_EVENT_DIALOG( p, n, num, c, o, r )	do { if ( rp_luaActive ) RP_Lua_EventDialog( p, n, num, c, o, r ); } while ( 0 )
#define RP_LUA_IN_CALL()				( rp_luaInCall != 0 )
#define RP_LUA_CMD_ROOM( c, n )			( !rp_luaInCall || RP_Lua_ScriptCmdRoom( c, n ) )
#define RP_LUA_BROADCAST_ROOM( n )		( !rp_luaInCall || RP_Lua_ScriptBroadcastRoom( n ) )
#define RP_LUA_DB_WRITE_OK()			( !rp_luaInCall || RP_Lua_ScriptDbWrite() )
