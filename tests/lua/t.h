// TEST ONLY: shared by the test programs
#include "../../galaxyrp/game/rp_lua_local.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

extern char t_log[]; extern int t_logLen; extern char t_sent[MAX_CLIENTS][64][1100]; extern int t_nsent[MAX_CLIENTS];
extern char t_console[]; extern int t_ms; extern qboolean t_clockFrozen; extern int t_tick; extern int t_argc; extern char t_argv[64][256];
extern int t_floodProtect; extern qboolean t_builtin;
extern int t_freeSlots, t_poolFree, t_useDepth, t_useChain, t_useRefused, t_effects, t_sounds, t_lastEffect, t_lastSound, t_freed, t_npcsAlive, t_removedSilently; extern qboolean t_spawnRefuse, t_npcRefuse, t_locked[]; extern int t_usedCount[], t_usedInCall[]; extern gentity_t *t_usedActivator[], *t_usedOther[]; extern const char *t_cs[2][64]; extern int t_ncs[2]; extern char t_record[MAX_ENTITIESTOTAL][64][2][260]; extern int t_recordCount[];
extern gclient_t g_clients_store[]; extern int t_freedRidden, t_vehiclesAlive; extern char t_lastSpawnerClass[]; void t_ride( gentity_t *veh, gentity_t *pilot, int passengers ); extern vec3_t t_lastEffectOrigin;
int t_mapEntity( const char *classname, const char *targetname, const char *target ); void t_runThinks( void );
int t_rewardEntity( const char *targetname, const char *target, int credits, int spawnflags ); extern int t_paidCredits[MAX_CLIENTS], t_payouts, t_musicChanges, t_teleports, t_teleportedTo;
const char *t_recordValue( int n, const char *key ); void t_spawnReal( gentity_t *ent );
void t_key( const char *k, const char *v ); void t_keysClear( void ); extern qboolean t_esMade;	// spawn keys for the next SP_ call
void t_init( int maxclients ); void t_player( int i, const char *name, qboolean loggedin, int charId ); void t_frame( int ms );

static int fails;
#define CHECK( c, ... ) do { if ( !(c) ) { fails++; printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

static void t_mkdirs( const char *path ) { char b[512], *p; Q_strncpyz( b, path, sizeof( b ) ); for ( p = b + 1; *p; p++ ) if ( *p == '/' ) { *p = 0; mkdir( b, 0755 ); *p = '/'; } mkdir( b, 0755 ); }
static void t_write_plugin( const char *name, const char *code )
{
	char dir[512], path[600]; FILE *f;
	Com_sprintf( dir, sizeof( dir ), "./home/" RP_MOD_FOLDER "/lua/plugins/%s", name ); t_mkdirs( dir ); t_mkdirs( "./home/" RP_MOD_FOLDER "/database" );
	Com_sprintf( path, sizeof( path ), "%s/plugin.lua", dir ); f = fopen( path, "wb" ); fputs( code, f ); fclose( f );
}
static void t_start( const char *plugins ) { Q_strncpyz( rp_lua_plugins.string, plugins, sizeof( rp_lua_plugins.string ) ); RP_LUA_INIT(); if ( getenv( "T_DEBUG" ) ) printf( "--- log after start:\n%s", t_log ); }
static int t_sent_count( int c ) { return t_nsent[c]; }
static void t_clear_sent( void ) { memset( t_nsent, 0, sizeof( t_nsent ) ); }
static int t_log_has( const char *s ) { return strstr( t_log, s ) != NULL; }
static void t_args( const char *line ) { char b[1024], *p, *s; Q_strncpyz( b, line, sizeof( b ) ); t_argc = 0; for ( s = b; ( p = strtok( s, " " ) ) != NULL; s = NULL ) Q_strncpyz( t_argv[t_argc++], p, 256 ); }
static void t_fresh( void ) { system( "rm -rf ./home" ); }
