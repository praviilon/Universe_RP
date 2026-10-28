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

void t_worldReset( void );
void QDECL G_LogPrintf( const char *fmt, ... ) { va_list a; char b[2048]; va_start( a, fmt ); Q_vsnprintf( b, sizeof( b ), fmt, a ); va_end( a ); if ( t_logLen + (int)strlen( b ) < (int)sizeof( t_log ) - 1 ) { strcpy( t_log + t_logLen, b ); t_logLen += strlen( b ); } }
static void QDECL s_Print( const char *fmt, ... ) { va_list a; char b[2048]; va_start( a, fmt ); Q_vsnprintf( b, sizeof( b ), fmt, a ); va_end( a ); Q_strcat( t_console, sizeof( t_console ), b ); }
static void QDECL s_Error( int level, const char *fmt, ... ) { printf( "G_Error: %s\n", fmt ); exit( 1 ); }
static void s_Send( int clientNum, const char *text ) { if ( clientNum < 0 ) { int i; for ( i = 0; i < MAX_CLIENTS; i++ ) s_Send( i, text ); return; } if ( t_nsent[clientNum] < 64 ) Q_strncpyz( t_sent[clientNum][t_nsent[clientNum]++], text, 1100 ); }
static int t_slow = -1;	// T_SLOW=n: the clock runs n times slower (valgrind), so the budgets stay meaningful
static int s_Ms( void ) { if ( !t_clockFrozen ) { struct timeval tv; long long ms; if ( t_slow < 0 ) t_slow = getenv( "T_SLOW" ) ? atoi( getenv( "T_SLOW" ) ) : 1; gettimeofday( &tv, NULL ); ms = tv.tv_sec * 1000LL + tv.tv_usec / 1000; return (int)( ms / ( t_slow > 0 ? t_slow : 1 ) ); } t_ms += t_tick; return t_ms; }
static void s_Link( sharedEntity_t *e ) { (void)e; }
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
	imp.LinkEntity = s_Link; imp.UnlinkEntity = s_Link; imp.Argc = s_Argc; imp.Argv = s_Argv; imp.Cvar_VariableStringBuffer = s_CvarStr; imp.Cvar_VariableIntegerValue = s_CvarInt;
	for ( i = MAX_CLIENTS; i < MAX_ENTITIESTOTAL; i++ ) { if ( g_entities[i].inuse && g_entities[i].client && ( g_entities[i].client < g_clients_store || g_entities[i].client >= g_clients_store + MAX_CLIENTS ) ) free( g_entities[i].client ); }
	memset( &level, 0, sizeof( level ) ); memset( g_entities, 0, sizeof( g_entities ) ); memset( g_clients_store, 0, sizeof( g_clients_store ) );
	level.maxclients = maxclients; level.num_entities = MAX_CLIENTS; level.time = 1000;
	Q_strncpyz( level.rawmapname, "testmap", sizeof( level.rawmapname ) );
	for ( i = 0; i < MAX_CLIENTS; i++ ) { g_entities[i].client = &g_clients_store[i]; }
	rp_lua.integer = 1; rp_lua_memkb.integer = 512; Q_strncpyz( rp_lua_plugins.string, "", sizeof( rp_lua_plugins.string ) );
	t_logLen = 0; t_log[0] = 0; t_console[0] = 0; memset( t_nsent, 0, sizeof( t_nsent ) );
	t_worldReset();
}
void t_player( int i, const char *name, qboolean loggedin, int charId )
{
	gentity_t *e = &g_entities[i];
	e->inuse = qtrue; e->client->pers.connected = CON_CONNECTED; Q_strncpyz( e->client->pers.netname, name, sizeof( e->client->pers.netname ) );
	e->client->sess.loggedin = loggedin; e->client->pers.CharID = charId; Q_strncpyz( e->client->sess.filename, va( "acc%d", i ), 32 );
	e->client->pers.level = 5; e->client->pers.credits = 100; e->health = 100; e->client->sess.sessionTeam = TEAM_FREE;
}
void t_frame( int ms ) { level.time += ms; RP_LUA_FRAME(); }

/*
==================
P2: the world, stubbed as recording fakes. The real spawn code needs the whole game; these keep the
shape (slots, records, refusals, locks, the use chain) so rp_lua_world.c is exercised end to end.
==================
*/
int t_freeSlots = 1000;			// G_EntitySlotsAvailable: free slots past the reserve
int t_poolFree = 1 << 20;		// G_AllocRoomFor
qboolean t_spawnRefuse = qfalse;	// zyk_main_spawn_entity frees the entity with a reason
qboolean t_locked[MAX_ENTITIESTOTAL];	// GlobalUse's lock: refuses every player activator
int t_usedCount[MAX_ENTITIESTOTAL]; gentity_t *t_usedActivator[MAX_ENTITIESTOTAL], *t_usedOther[MAX_ENTITIESTOTAL];
int t_useDepth, t_useChain, t_useRefused;
char t_record[MAX_ENTITIESTOTAL][64][2][260]; int t_recordCount[MAX_ENTITIESTOTAL];
int t_effects, t_sounds; int t_lastEffect, t_lastSound; vec3_t t_lastEffectOrigin;
const char *t_cs[2][64]; int t_ncs[2];	// [0] sounds, [1] effects
int t_freed;				// G_FreeEntity calls on entities above the clients
int t_npcsAlive;
qboolean t_npcRefuse = qfalse;	// NPC_Spawn_Do returns NULL
int t_removedSilently;

static const char *s_exempt[] = { "misc_model_breakable", "misc_model_ghoul", "fx_runner", "target_speaker", "weapon_blaster", "ammo_blaster", "item_medpac", "misc_exploding_crate", NULL };
qboolean RP_MapExemptClass( const char *classname ) { int i; for ( i = 0; s_exempt[i]; i++ ) if ( !Q_stricmp( classname, s_exempt[i] ) ) return qtrue; return qfalse; }
static const char *s_vector[] = { "origin", "angles", "angles2", "mins", "maxs", "trdelta", "color", "offcolor", "modelscale_vec", "spinangles", "tint", NULL };
qboolean RP_EntityVectorKey( const char *key ) { int i; for ( i = 0; s_vector[i]; i++ ) if ( !Q_stricmp( key, s_vector[i] ) ) return qtrue; return qfalse; }
qboolean RP_NpcTypeKnown( const char *type ) { return ( !Q_stricmp( type, "jedi" ) || !Q_stricmp( type, "stormtrooper" ) || !Q_stricmp( type, "swoop" ) ) ? qtrue : qfalse; }
qboolean RP_NpcTypeIsVehicle( const char *type ) { return !Q_stricmp( type, "swoop" ) ? qtrue : qfalse; }
void RP_NpcPrecacheByType( const char *type ) { (void)type; }
static char *s_strings[65536]; static int s_numStrings;
char *G_NewString( const char *s ) { char *d = strdup( s ); if ( s_numStrings < 65536 ) s_strings[s_numStrings++] = d; return d; }
qboolean G_EntitySlotsAvailable( int needed ) { return t_freeSlots >= needed ? qtrue : qfalse; }
qboolean G_AllocRoomFor( int bytes ) { return t_poolFree >= bytes ? qtrue : qfalse; }
int RP_PairsBytes( char **pairs, int count ) { int i, n = 0; for ( i = 0; i < count; i++ ) n += strlen( pairs[i] ) + 1; return n; }
void RP_SpawnRouteInit( rpSpawnRoute_t *r ) { memset( r, 0, sizeof( *r ) ); }
void RP_SpawnRouteNoteKey( rpSpawnRoute_t *r, const char *key, const char *value ) { if ( !Q_stricmp( key, "classname" ) ) Q_strncpyz( r->classname, value, sizeof( r->classname ) ); }
qboolean RP_SpawnRouteHasRoom( const rpSpawnRoute_t *r ) { (void)r; return G_EntitySlotsAvailable( 4 ); }
static gentity_t *s_SpawnSlot( void ) { int i; for ( i = MAX_CLIENTS + 8; i < MAX_GENTITIES - 1; i++ ) { if ( !g_entities[i].inuse ) { memset( &g_entities[i], 0, sizeof( gentity_t ) ); g_entities[i].inuse = qtrue; g_entities[i].s.number = i; t_recordCount[i] = 0; if ( i >= level.num_entities ) level.num_entities = i + 1; t_freeSlots--; return &g_entities[i]; } } return NULL; }
gentity_t *RP_SpawnForRoute( const rpSpawnRoute_t *r ) { (void)r; return s_SpawnSlot(); }
gentity_t *RP_SpawnForClassname( const char *classname, qboolean nological, qboolean hasScript ) { gentity_t *e = s_SpawnSlot(); (void)nological; (void)hasScript; if ( e ) e->classname = (char *)classname; return e; }
void zyk_main_set_entity_field( gentity_t *ent, char *key, char *value ) { int n = ent->s.number; if ( !ent->inuse || t_recordCount[n] >= 64 ) return; Q_strncpyz( t_record[n][t_recordCount[n]][0], key, 260 ); Q_strncpyz( t_record[n][t_recordCount[n]][1], value, 260 ); t_recordCount[n]++; }
static const char *s_RecordValue( int n, const char *key ) { int i; for ( i = 0; i < t_recordCount[n]; i++ ) if ( !Q_stricmp( t_record[n][i][0], key ) ) return t_record[n][i][1]; return NULL; }
void G_FreeEntity( gentity_t *ed ) { int n = ed->s.number; if ( n >= MAX_CLIENTS ) t_freed++; if ( ed->client && n >= MAX_CLIENTS && ( ed->client < g_clients_store || ed->client >= g_clients_store + MAX_CLIENTS ) ) { free( ed->client ); t_npcsAlive--; } memset( ed, 0, sizeof( *ed ) ); ed->s.number = n; ed->classname = "freed"; t_recordCount[n] = 0; t_freeSlots++; }
void zyk_main_spawn_entity( gentity_t *ent ) { int n = ent->s.number; const char *v; if ( !ent->inuse ) return; level.rp_spawn_refusal[0] = 0; ent->classname = G_NewString( s_RecordValue( n, "classname" ) ); if ( ( v = s_RecordValue( n, "targetname" ) ) ) ent->targetname = G_NewString( v ); if ( ( v = s_RecordValue( n, "origin" ) ) ) sscanf( v, "%f %f %f", &ent->r.currentOrigin[0], &ent->r.currentOrigin[1], &ent->r.currentOrigin[2] ); if ( ( v = s_RecordValue( n, "health" ) ) ) ent->health = atoi( v ); ent->use = (void *)1; if ( t_spawnRefuse ) { Q_strncpyz( level.rp_spawn_refusal, "the server does not have that model", sizeof( level.rp_spawn_refusal ) ); G_FreeEntity( ent ); } }
int RP_FreeEntityChildren( gentity_t *maker ) { (void)maker; return 0; }
qboolean RP_EntityIsCharacter( const gentity_t *e ) { return ( e && e->client ) ? qtrue : qfalse; }
void G_SetOrigin( gentity_t *ent, vec3_t origin ) { VectorCopy( origin, ent->r.currentOrigin ); VectorCopy( origin, ent->s.origin ); }
gentity_t *NPC_Spawn_Do( gentity_t *spawner ) { gentity_t *npc; if ( t_npcRefuse || !G_EntitySlotsAvailable( 3 ) ) return NULL; npc = s_SpawnSlot(); if ( !npc ) return NULL; npc->client = calloc( 1, sizeof( gclient_t ) ); npc->classname = "NPC"; npc->NPC_type = spawner->NPC_type; npc->targetname = spawner->NPC_targetname; VectorCopy( spawner->s.origin, npc->r.currentOrigin ); npc->health = 100; npc->use = (void *)1; t_npcsAlive++; return npc; }
void RP_SpawnedRemove( gentity_t *e ) { e->takedamage = qfalse; e->health = 0; e->targetname = NULL; e->think = G_FreeEntity; e->nextthink = level.time + 100; t_removedSilently++; }
int RP_ConfigstringIndexOf( const char *name, int start, int max ) { int b = ( start == CS_SOUNDS ) ? 0 : 1, i; (void)max; for ( i = 0; i < t_ncs[b]; i++ ) if ( !Q_stricmp( t_cs[b][i], name ) ) return i + 1; return 0; }
gentity_t *G_PlayEffectID( const int fxID, vec3_t org, vec3_t ang ) { (void)ang; t_effects++; t_lastEffect = fxID; VectorCopy( org, t_lastEffectOrigin ); return NULL; }
void G_SoundAtLoc( vec3_t loc, int channel, int soundIndex ) { (void)loc; (void)channel; t_sounds++; t_lastSound = soundIndex; }
static void s_Use( gentity_t *self, gentity_t *other, gentity_t *activator ) { int n = self->s.number; t_usedCount[n]++; t_usedOther[n] = other; t_usedActivator[n] = activator; if ( self->target ) { gentity_t *t = NULL; while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) ) GlobalUse( t, self, activator ); } }
void GlobalUse( gentity_t *self, gentity_t *other, gentity_t *activator )
{ // the real one's shape: inactive, no use, the lock with a player activator, depth 128 / chain 2048, then use, then the Lua event
	if ( !self || ( self->flags & FL_INACTIVE ) || !self->use ) return;
	if ( other != self && t_locked[self->s.number] && activator && activator->client ) return;
	if ( t_useDepth == 0 ) t_useChain = 0;
	if ( t_useDepth >= 128 || t_useChain >= 2048 ) { t_useRefused++; return; }
	t_useDepth++; t_useChain++;
	s_Use( self, other, activator );
	t_useDepth--;
	RP_LUA_EVENT_USED( self, activator );
}
// a stand-in map entity with a use (a button, a relay): number returned
int t_mapEntity( const char *classname, const char *targetname, const char *target ) { gentity_t *e = s_SpawnSlot(); e->classname = (char *)classname; e->targetname = (char *)targetname; e->target = (char *)target; e->use = (void *)1; return e->s.number; }
// the think the real game would run next frame for an NPC taken away
void t_runThinks( void ) { int i; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) { gentity_t *e = &g_entities[i]; if ( e->inuse && e->think == G_FreeEntity && e->nextthink <= level.time ) G_FreeEntity( e ); } }

void t_worldReset( void )
{
	int i;
	for ( i = 0; i < s_numStrings; i++ ) free( s_strings[i] );
	s_numStrings = 0;
	t_freeSlots = 1000; t_poolFree = 1 << 20; t_spawnRefuse = qfalse; t_npcRefuse = qfalse;
	memset( t_locked, 0, sizeof( t_locked ) ); memset( t_usedCount, 0, sizeof( t_usedCount ) ); memset( t_recordCount, 0, sizeof( t_recordCount ) );
	t_useDepth = t_useChain = t_useRefused = 0; t_effects = t_sounds = 0; t_ncs[0] = t_ncs[1] = 0; t_freed = 0; t_npcsAlive = 0; t_removedSilently = 0;
}
