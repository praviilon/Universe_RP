// TEST ONLY: what rp_lua_*.c need from the game, stubbed. Built by build.sh with the real rp_lua_*.c,
// the vendored Lua and sqlite, never into the game.
#include "../../galaxyrp/game/rp_lua_local.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

level_locals_t level;
gentity_t g_entities[MAX_ENTITIESTOTAL];
gclient_t g_clients_store[MAX_CLIENTS];
gameImport_t *trap;
static gameImport_t imp;
vmCvar_t rp_lua, rp_lua_plugins, rp_lua_memkb;

char t_log[65536]; int t_logLen;
char t_sent[MAX_CLIENTS][64][1100]; int t_nsent[MAX_CLIENTS];
char t_console[8192];
int t_ms = 100000;				// the real clock (trap->Milliseconds)
qboolean t_clockFrozen = qfalse;
int t_tick = 0;				// frozen clock: ms added on every read (a deterministic "slow" script)
int t_argc; char t_argv[64][256];
int t_floodProtect = 1;
qboolean t_builtin = qfalse;

void QDECL G_LogPrintf( const char *fmt, ... ) { va_list a; char b[2048]; va_start( a, fmt ); Q_vsnprintf( b, sizeof( b ), fmt, a ); va_end( a ); if ( t_logLen + (int)strlen( b ) < (int)sizeof( t_log ) - 1 ) { strcpy( t_log + t_logLen, b ); t_logLen += strlen( b ); } }
static void QDECL s_Print( const char *fmt, ... ) { va_list a; char b[2048]; va_start( a, fmt ); Q_vsnprintf( b, sizeof( b ), fmt, a ); va_end( a ); Q_strcat( t_console, sizeof( t_console ), b ); }
static void QDECL s_Error( int level, const char *fmt, ... ) { printf( "G_Error: %s\n", fmt ); exit( 1 ); }
static void s_Send( int clientNum, const char *text ) { if ( clientNum < 0 ) { int i; for ( i = 0; i < MAX_CLIENTS; i++ ) s_Send( i, text ); return; } if ( t_nsent[clientNum] < 64 ) Q_strncpyz( t_sent[clientNum][t_nsent[clientNum]++], text, 1100 ); }
static int t_slow = -1;	// T_SLOW=n: the clock runs n times slower (valgrind), so the budgets stay meaningful
static int s_Ms( void ) { if ( !t_clockFrozen ) { struct timeval tv; long long ms; if ( t_slow < 0 ) t_slow = getenv( "T_SLOW" ) ? atoi( getenv( "T_SLOW" ) ) : 1; gettimeofday( &tv, NULL ); ms = tv.tv_sec * 1000LL + tv.tv_usec / 1000; return (int)( ms / ( t_slow > 0 ? t_slow : 1 ) ); } t_ms += t_tick; return t_ms; }
static int s_Argc( void ) { return t_argc; }
static void s_Argv( int n, char *buf, int size ) { Q_strncpyz( buf, n < t_argc ? t_argv[n] : "", size ); }
static void s_CvarStr( const char *name, char *buf, int size ) { if ( !strcmp( name, "fs_homepath" ) ) Q_strncpyz( buf, "./home", size ); else Q_strncpyz( buf, "", size ); }
static int s_CvarInt( const char *name ) { if ( !strcmp( name, "sv_floodProtect" ) ) return t_floodProtect; return 0; }
NORETURN_PTR void (*Com_Error)( int level, const char *error, ... ) = (void *)s_Error;
void (*Com_Printf)( const char *msg, ... ) = s_Print;

gentity_t *G_Find( gentity_t *from, int fieldofs, const char *match ) { if ( !from ) from = g_entities; else from++; for ( ; from < &g_entities[level.num_entities]; from++ ) { char *s; if ( !from->inuse ) continue; s = *(char **)( (byte *)from + fieldofs ); if ( s && !Q_stricmp( s, match ) ) return from; } return NULL; }
qboolean check_admin_command( gentity_t *ent, int bit, qboolean msg ) { return ( ent->client->pers.bitvalue & ( 1 << bit ) ) ? qtrue : qfalse; }
char *ConcatArgs( int start ) { static char b[1024]; int i; b[0] = 0; for ( i = start; i < t_argc; i++ ) { Q_strcat( b, sizeof( b ), t_argv[i] ); if ( i < t_argc - 1 ) Q_strcat( b, sizeof( b ), " " ); } return b; }
int RP_AdminBitByTitle( const char *title ) { return Q_stricmp( title, "Scripts" ) ? ( Q_stricmp( title, "Give" ) ? -1 : 3 ) : 29; }
qboolean RP_IsBuiltInCommand( const char *name ) { return ( !Q_stricmp( name, "say" ) || !Q_stricmp( name, "kill" ) || t_builtin ) ? qtrue : qfalse; }

void t_init( int maxclients )
{
	int i;
	setvbuf( stdout, NULL, _IONBF, 0 );
	trap = &imp;
	imp.Print = s_Print; imp.Error = s_Error; imp.SendServerCommand = s_Send; imp.Milliseconds = s_Ms;
	imp.Argc = s_Argc; imp.Argv = s_Argv; imp.Cvar_VariableStringBuffer = s_CvarStr; imp.Cvar_VariableIntegerValue = s_CvarInt;
	memset( &level, 0, sizeof( level ) ); memset( g_entities, 0, sizeof( g_entities ) ); memset( g_clients_store, 0, sizeof( g_clients_store ) );
	level.maxclients = maxclients; level.num_entities = MAX_CLIENTS; level.time = 1000;
	Q_strncpyz( level.rawmapname, "testmap", sizeof( level.rawmapname ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) { g_entities[i].client = &g_clients_store[i]; }
	rp_lua.integer = 1; rp_lua_memkb.integer = 512; Q_strncpyz( rp_lua_plugins.string, "", sizeof( rp_lua_plugins.string ) );
	t_logLen = 0; t_log[0] = 0; t_console[0] = 0; memset( t_nsent, 0, sizeof( t_nsent ) );
}
void t_player( int i, const char *name, qboolean loggedin, int charId )
{
	gentity_t *e = &g_entities[i];
	e->inuse = qtrue; e->client->pers.connected = CON_CONNECTED; Q_strncpyz( e->client->pers.netname, name, sizeof( e->client->pers.netname ) );
	e->client->sess.loggedin = loggedin; e->client->pers.CharID = charId; Q_strncpyz( e->client->sess.filename, va( "acc%d", i ), 32 );
	e->client->pers.level = 5; e->client->pers.credits = 100; e->health = 100; e->client->sess.sessionTeam = TEAM_FREE;
}
void t_frame( int ms ) { level.time += ms; RP_LUA_FRAME(); }
