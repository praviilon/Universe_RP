/*
===========================================================================
GLX_U: [Lua] P2 -- the world, as a player reaches it. Every call here is an existing game function a
player or an admin command already runs, with that function's own checks, and a token from the same
bucket the messages use: a plugin acts at a player's pace.

  World.use( targetname, player )         GlobalUse() on each entity of that name, the player as activator:
                                          locks, depth and chain limits as for a button; no admin bypass;
                                          rewards pay and dialogues open, as for a button
  World.prop( class, origin, angles, keys ) the /entadd sequence, for the Entity System's exempt classes
  World.npc( type, origin, angles, opts ) a spawner and NPC_Spawn_Do(), as /npc spawn; opts are an npc_spawner's
                                          keys (kill reward, health, team, effect, freeze, CINEMATIC, scale, hat, cape)
  World.vehicle( type, origin, angles, opts ) the same for a vehicle, as a map's NPC_Vehicle spawner (team, scale)
  World.remove( handle )                  queued: freed at the end of the frame, never inside a call
  Effect.play( name, origin, angles )     G_PlayEffectID() of an effect already registered
  Sound.play( name, origin )              G_SoundAtLoc() of a sound already registered

A spawned entity carries the plugin's slot and a stamp (gentity_t::rpLuaPlugin, rpLuaStamp): /entsave leaves
it out, a handle to a reused slot never matches, and unload, reload, disable and the map's end take every
entity the plugin made (RP_LuaWorld_PluginClose, from RP_Lua_ClosePlugin, which runs at a frame end only).
A vehicle someone rides is never removed: World.remove refuses it, and at a close it is released instead (its
tag becomes RP_LUA_RELEASED) and removed by RP_LuaWorld_ReleasedFrame once nobody rides it. Freeing a ridden
vehicle would throw its riders off, and kill them on a vehicle whose file has killRiderOnDeath (EjectAll()).
===========================================================================
*/
#include "rp_lua_local.h"
#include "../../codemp/game/b_local.h"	// SFB_CINEMATIC: World.npc's noai option

extern void zyk_main_set_entity_field( gentity_t *ent, char *key, char *value );	// g_spawn.c
extern void zyk_main_spawn_entity( gentity_t *ent );
extern gentity_t *NPC_Spawn_Do( gentity_t *ent );	// NPC_spawn.c

#define RP_LUA_ENTITY_MT "rp.entity"

typedef struct {
	int		number;
	int		stamp;
	int		plugin;		// slot + 1
} rpLuaEntityHandle_t;

static int rpLuaStampNext = 0;

// ridden vehicles their plugin let go of: removed once empty
typedef struct { int number, stamp; } rpLuaReleased_t;
static rpLuaReleased_t rpLuaReleased[RP_LUA_VEHICLES_SERVER];
static int rpLuaNumReleased;
static int rpLuaReleasedCheck;		// level.time of the next look
#define RP_LUA_RELEASED_CHECK_MS	500

// a vehicle with a pilot or passengers
static qboolean RP_LuaWorld_Ridden( const gentity_t *ent )
{
	return ( ent->m_pVehicle && ( ent->m_pVehicle->m_pPilot || ent->m_pVehicle->m_iNumPassengers > 0 ) ) ? qtrue : qfalse;
}

/*
==================
The plugin's list
==================
*/
static qboolean RP_LuaWorld_Live( const rpLuaEntity_t *e )
{
	const gentity_t *ent;

	if ( e->number < 0 || e->number >= MAX_ENTITIESTOTAL )
		return qfalse;
	ent = &g_entities[e->number];
	return ( ent->inuse && ent->rpLuaStamp == e->stamp && e->stamp ) ? qtrue : qfalse;
}

// drops the entries whose entity is gone (broken, /entremove'd, cleared by /entload, killed)
static void RP_LuaWorld_Compact( rpLuaPlugin_t *p )
{
	int i, kept = 0;

	for ( i = 0; i < p->numEnts; i++ )
	{
		if ( RP_LuaWorld_Live( &p->ents[i] ) )
			p->ents[kept++] = p->ents[i];
	}
	p->numEnts = kept;
}

int RP_LuaWorld_CountLive( rpLuaPlugin_t *p, qboolean npcs )
{
	int i, n = 0;

	RP_LuaWorld_Compact( p );
	for ( i = 0; i < p->numEnts; i++ )
	{
		if ( p->ents[i].npc == npcs && !p->ents[i].vehicle )
			n++;
	}
	return n;
}

int RP_LuaWorld_CountVehicles( rpLuaPlugin_t *p )
{
	int i, n = 0;

	RP_LuaWorld_Compact( p );
	for ( i = 0; i < p->numEnts; i++ )
	{
		if ( p->ents[i].vehicle )
			n++;
	}
	return n;
}

static qboolean RP_LuaWorld_ReleasedLive( const rpLuaReleased_t *r )
{
	const gentity_t *ent;

	if ( r->number < 0 || r->number >= MAX_ENTITIESTOTAL )
		return qfalse;
	ent = &g_entities[r->number];
	return ( ent->inuse && r->stamp && ent->rpLuaStamp == r->stamp && ent->rpLuaPlugin == RP_LUA_RELEASED ) ? qtrue : qfalse;
}

// every plugin's live vehicles and the released ones still about
static int RP_LuaWorld_VehiclesOnServer( void )
{
	int i, n = 0;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		if ( rpLuaPlugins[i].inUse )
			n += RP_LuaWorld_CountVehicles( &rpLuaPlugins[i] );
	}
	for ( i = 0; i < rpLuaNumReleased; i++ )
	{
		if ( RP_LuaWorld_ReleasedLive( &rpLuaReleased[i] ) )
			n++;
	}
	return n;
}

rpLuaPlugin_t *RP_LuaWorld_OwnerOf( const gentity_t *ent )
{
	int slot;

	if ( !ent || !ent->rpLuaPlugin )
		return NULL;
	slot = ent->rpLuaPlugin - 1;
	if ( slot < 0 || slot >= RP_LUA_MAX_PLUGINS || !rpLuaPlugins[slot].inUse )
		return NULL;
	return &rpLuaPlugins[slot];
}

static void RP_LuaWorld_Tag( rpLuaPlugin_t *p, gentity_t *ent, qboolean npc, qboolean vehicle )
{
	rpLuaEntity_t *e;

	ent->rpLuaPlugin = (int)( p - rpLuaPlugins ) + 1;
	ent->rpLuaStamp = ++rpLuaStampNext;
	if ( p->numEnts >= RP_LUA_MAX_ENTS )
		RP_LuaWorld_Compact( p );
	if ( p->numEnts >= RP_LUA_MAX_ENTS )
		return;	// cannot happen: the caps are checked before a spawn
	e = &p->ents[p->numEnts++];
	e->number = ent - g_entities;
	e->stamp = ent->rpLuaStamp;
	e->npc = npc;
	e->vehicle = vehicle;
	e->removeQueued = qfalse;
}

// the plugin lets a ridden vehicle go: no plugin's now, still a script's, removed once empty
static void RP_LuaWorld_Release( gentity_t *ent )
{
	int i, kept = 0;

	for ( i = 0; i < rpLuaNumReleased; i++ )
	{ // drop the ones gone since the last look first: the live ones and this one are within the server's cap
		if ( RP_LuaWorld_ReleasedLive( &rpLuaReleased[i] ) )
			rpLuaReleased[kept++] = rpLuaReleased[i];
	}
	rpLuaNumReleased = kept;
	ent->rpLuaPlugin = RP_LUA_RELEASED;
	if ( rpLuaNumReleased >= RP_LUA_VEHICLES_SERVER )
	{ // cannot happen (released ones count toward the server's cap); it then stays until the map ends
		G_LogPrintf( "[lua] released vehicle %d not tracked: the list is full\n", ent->s.number );
		return;
	}
	rpLuaReleased[rpLuaNumReleased].number = ent->s.number;
	rpLuaReleased[rpLuaNumReleased].stamp = ent->rpLuaStamp;
	rpLuaNumReleased++;
}

// the one place an entity a script made is freed: at a frame end (RP_LuaWorld_Frame, RP_Lua_RunQueue) or
// at the map's end (RP_Lua_Shutdown) -- never inside a script call;
// a ridden vehicle is never freed: with releaseRidden it is released, otherwise it stays the plugin's
static void RP_LuaWorld_FreeNow( rpLuaEntity_t *e, qboolean releaseRidden )
{
	gentity_t *ent;

	if ( !RP_LuaWorld_Live( e ) )
		return;
	ent = &g_entities[e->number];
	if ( e->vehicle && RP_LuaWorld_Ridden( ent ) )
	{
		if ( releaseRidden )
			RP_LuaWorld_Release( ent );
		return;
	}
	if ( e->npc && ent->client )
	{
		RP_SpawnedRemove( ent );	// as /entundo of a spawner: silent, nothing fires, gone next frame
		ent->rpLuaPlugin = 0;		// and no longer the script's: a stale handle from here
		ent->rpLuaStamp = 0;
	}
	else
	{
		RP_FreeEntityChildren( ent );
		G_FreeEntity( ent );
	}
}

void RP_LuaWorld_Frame( rpLuaPlugin_t *p )
{
	int i;

	for ( i = 0; i < p->numEnts; i++ )
	{
		if ( p->ents[i].removeQueued )
		{ // a vehicle boarded since World.remove stays the plugin's
			RP_LuaWorld_FreeNow( &p->ents[i], qfalse );
			p->ents[i].removeQueued = qfalse;
		}
	}
	p->removesQueued = qfalse;
	RP_LuaWorld_Compact( p );
}

void RP_LuaWorld_PluginClose( rpLuaPlugin_t *p )
{
	int i;

	for ( i = 0; i < p->numEnts; i++ )
		RP_LuaWorld_FreeNow( &p->ents[i], qtrue );
	p->numEnts = 0;
	p->removesQueued = qfalse;
}

// from RP_Lua_Frame, every frame, looking every 500 ms: a released vehicle nobody rides goes (its droid with it);
// one destroyed, removed by an admin or otherwise gone is dropped from the list
void RP_LuaWorld_ReleasedFrame( void )
{
	int i, kept = 0;

	if ( !rpLuaNumReleased || level.time < rpLuaReleasedCheck )
		return;
	rpLuaReleasedCheck = level.time + RP_LUA_RELEASED_CHECK_MS;
	for ( i = 0; i < rpLuaNumReleased; i++ )
	{
		rpLuaReleased_t *r = &rpLuaReleased[i];
		gentity_t *ent;

		if ( !RP_LuaWorld_ReleasedLive( r ) )
			continue;
		ent = &g_entities[r->number];
		if ( RP_LuaWorld_Ridden( ent ) )
		{
			rpLuaReleased[kept++] = *r;
			continue;
		}
		if ( ent->client )
			RP_SpawnedRemove( ent );	// not drawn, not solid, gone next frame; nobody aboard, so no EjectAll
		else
			G_FreeEntity( ent );
		ent->rpLuaPlugin = 0;
		ent->rpLuaStamp = 0;
	}
	rpLuaNumReleased = kept;
}

void RP_LuaWorld_Reset( void )
{
	rpLuaNumReleased = 0;
	rpLuaReleasedCheck = 0;
}

/*
==================
Handles
==================
*/
void RP_LuaWorld_PushEntity( lua_State *L, const gentity_t *ent )
{
	rpLuaEntityHandle_t *h = (rpLuaEntityHandle_t *)lua_newuserdatauv( L, sizeof( *h ), 0 );

	h->number = ent - g_entities;
	h->stamp = ent->rpLuaStamp;
	h->plugin = ent->rpLuaPlugin;
	luaL_setmetatable( L, RP_LUA_ENTITY_MT );
}

// the entity behind a handle while it is alive and this plugin's, else NULL
static gentity_t *RP_LuaWorld_Handle( lua_State *L, int idx )
{
	rpLuaEntityHandle_t *h = (rpLuaEntityHandle_t *)luaL_checkudata( L, idx, RP_LUA_ENTITY_MT );
	gentity_t *ent;

	if ( h->number < 0 || h->number >= MAX_ENTITIESTOTAL || !rpLuaCurrent )
		return NULL;
	ent = &g_entities[h->number];
	if ( !ent->inuse || ent->rpLuaStamp != h->stamp || !h->stamp || ent->rpLuaPlugin != (int)( rpLuaCurrent - rpLuaPlugins ) + 1 )
		return NULL;
	return ent;
}

static int RP_LuaWorld_EntityValid( lua_State *L )
{
	lua_pushboolean( L, RP_LuaWorld_Handle( L, 1 ) != NULL );
	return 1;
}

static int RP_LuaWorld_EntityIndex( lua_State *L )
{
	gentity_t *ent = RP_LuaWorld_Handle( L, 1 );
	const char *key = luaL_checkstring( L, 2 );

	if ( !strcmp( key, "valid" ) )
	{
		lua_pushcfunction( L, RP_LuaWorld_EntityValid );
		return 1;
	}
	if ( !ent )
	{
		lua_pushnil( L );
		return 1;
	}
	if ( !strcmp( key, "number" ) )
		lua_pushinteger( L, ent - g_entities );
	else if ( !strcmp( key, "classname" ) )
		lua_pushstring( L, ent->classname ? ent->classname : "" );
	else if ( !strcmp( key, "npc_type" ) )
		lua_pushstring( L, ent->NPC_type ? ent->NPC_type : "" );
	else if ( !strcmp( key, "targetname" ) )
		lua_pushstring( L, ent->targetname ? ent->targetname : "" );
	else if ( !strcmp( key, "health" ) )
		lua_pushinteger( L, ent->health );
	else if ( ( !strcmp( key, "team" ) || !strcmp( key, "effect" ) || !strcmp( key, "frozen" ) || !strcmp( key, "scale" )
		|| !strcmp( key, "max_health" ) ) && !( ent->client && ent->s.number >= MAX_CLIENTS ) )
		lua_pushnil( L );	// NPCs and vehicles only: nil for a prop
	else if ( !strcmp( key, "team" ) )
	{ // the side it is on now (/npc team may have changed it)
		static const char *const teams[] = { "free", "enemy", "player", "neutral" };
		const int t = ent->client->playerTeam;

		if ( t >= NPCTEAM_FREE && t <= NPCTEAM_NEUTRAL )
			lua_pushstring( L, teams[t] );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "effect" ) )
	{ // the /npc effect it shows, or nil
		const int m = ent->client->pers.phase_mode;

		lua_pushstring( L, m == RP_PHASE_HOLO ? "holo" : m == RP_PHASE_GHOST ? "ghost" : m == RP_PHASE_NONSOLID ? "walkthrough" : NULL );
	}
	else if ( !strcmp( key, "frozen" ) )
		lua_pushboolean( L, RP_NpcIsFrozen( ent ) );
	else if ( !strcmp( key, "scale" ) )
		lua_pushinteger( L, ent->rpNpcScale > 0 ? ent->rpNpcScale : 100 );
	else if ( !strcmp( key, "max_health" ) )
		lua_pushinteger( L, ent->client->ps.stats[STAT_MAX_HEALTH] );
	else if ( !strcmp( key, "credits" ) || !strcmp( key, "xp" ) )
	{ // an NPC's kill reward, 0 once paid (a vehicle never has one); nil for a prop
		if ( ent->client && ent->s.number >= MAX_CLIENTS )
			lua_pushinteger( L, key[0] == 'c' ? ent->rpRewardCredits : ent->rpRewardXP );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "vehicle_type" ) )
	{
		static const char *const names[] = { "none", "walker", "fighter", "speeder", "animal", "flier" };
		int t = ( ent->m_pVehicle && ent->m_pVehicle->m_pVehicleInfo ) ? ent->m_pVehicle->m_pVehicleInfo->type : -1;

		if ( t > VH_NONE && t < VH_NUM_VEHICLES )
			lua_pushstring( L, names[t] );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "pilot" ) )
	{
		const gentity_t *pilot = ent->m_pVehicle ? (const gentity_t *)ent->m_pVehicle->m_pPilot : NULL;

		if ( pilot && pilot->client && pilot->s.number >= 0 && pilot->s.number < MAX_CLIENTS )
			RP_LuaApi_PushPlayer( L, pilot->s.number );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "passengers" ) )
	{
		if ( ent->m_pVehicle )
			lua_pushinteger( L, ent->m_pVehicle->m_iNumPassengers );
		else
			lua_pushnil( L );
	}
	else if ( !strcmp( key, "origin" ) )
	{
		lua_createtable( L, 0, 3 );
		lua_pushnumber( L, ent->r.currentOrigin[0] ); lua_setfield( L, -2, "x" );
		lua_pushnumber( L, ent->r.currentOrigin[1] ); lua_setfield( L, -2, "y" );
		lua_pushnumber( L, ent->r.currentOrigin[2] ); lua_setfield( L, -2, "z" );
	}
	else
		lua_pushnil( L );
	return 1;
}

static int RP_LuaWorld_EntityEq( lua_State *L )
{
	rpLuaEntityHandle_t *a = (rpLuaEntityHandle_t *)luaL_checkudata( L, 1, RP_LUA_ENTITY_MT );
	rpLuaEntityHandle_t *b = (rpLuaEntityHandle_t *)luaL_checkudata( L, 2, RP_LUA_ENTITY_MT );

	lua_pushboolean( L, a->number == b->number && a->stamp == b->stamp );
	return 1;
}

static int RP_LuaWorld_EntityToString( lua_State *L )
{
	rpLuaEntityHandle_t *h = (rpLuaEntityHandle_t *)luaL_checkudata( L, 1, RP_LUA_ENTITY_MT );

	lua_pushfstring( L, "entity(%d)", h->number );
	return 1;
}

/*
==================
Arguments
==================
*/
static int RP_LuaWorld_Refuse( lua_State *L, const char *why )
{
	lua_pushboolean( L, 0 );
	lua_pushstring( L, why );
	return 2;
}

// {x, y, z} of finite numbers within the map, or an error
// t[key] pushed, read raw: an __index metamethod would run script code in the middle of a spawn (and a
// World.prop from it would nest these frames until the C stack is gone)
static void RP_LuaWorld_RawField( lua_State *L, int idx, const char *key )
{
	idx = lua_absindex( L, idx );
	lua_pushstring( L, key );
	lua_rawget( L, idx );
}

static void RP_LuaWorld_Vec( lua_State *L, int idx, vec3_t out, const char *what )
{
	static const char *const names[3] = { "x", "y", "z" };
	int i;

	if ( !lua_istable( L, idx ) )
		luaL_error( L, "%s is a table {x=, y=, z=}", what );
	for ( i = 0; i < 3; i++ )
	{
		RP_LuaWorld_RawField( L, idx, names[i] );
		if ( !lua_isnumber( L, -1 ) )
			luaL_error( L, "%s.%s is not a number", what, names[i] );
		out[i] = (float)lua_tonumber( L, -1 );
		lua_pop( L, 1 );
		if ( !RP_FINITE( out[i] ) || out[i] < -RP_LUA_MAP_SIZE || out[i] > RP_LUA_MAP_SIZE )
			luaL_error( L, "%s.%s is out of the map", what, names[i] );
	}
}

// angles may be nil: zero then
static void RP_LuaWorld_Angles( lua_State *L, int idx, vec3_t out )
{
	if ( lua_isnoneornil( L, idx ) )
	{
		VectorClear( out );
		return;
	}
	RP_LuaWorld_Vec( L, idx, out, "angles" );
}

// a short name for lua.<plugin>.<name>: letters, digits and '_'
static qboolean RP_LuaWorld_NameOk( const char *name )
{
	int i;

	if ( !name[0] || strlen( name ) > RP_LUA_ENT_NAME_MAX )
		return qfalse;
	for ( i = 0; name[i]; i++ )
	{
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
			return qfalse;
	}
	return qtrue;
}

/*
==================
The spawn options of World.npc and World.vehicle (opts, the 4th argument): what an npc_spawner's or NPC_Vehicle's
keys give the NPC or vehicle it makes, checked as /entadd checks those keys. A wrong value is an argument error.
==================
*/
// opts.<key> pushed, or qfalse (nothing pushed) when there are no opts or no such key
static qboolean RP_LuaWorld_OptGet( lua_State *L, int idx, const char *key )
{
	if ( lua_isnoneornil( L, idx ) )
		return qfalse;
	luaL_checktype( L, idx, LUA_TTABLE );
	RP_LuaWorld_RawField( L, idx, key );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return qfalse;
	}
	return qtrue;
}

// opts.<key>: a whole number from min to max; def when absent
static int RP_LuaWorld_OptInt( lua_State *L, int idx, const char *key, int min, int max, int def )
{
	lua_Integer v;
	int isInt;

	if ( !RP_LuaWorld_OptGet( L, idx, key ) )
		return def;
	v = lua_tointegerx( L, -1, &isInt );
	if ( !isInt || lua_type( L, -1 ) != LUA_TNUMBER || v < min || v > max )
		luaL_error( L, "%s is a whole number from %d to %d", key, min, max );
	lua_pop( L, 1 );
	return (int)v;
}

// opts.<key>: true or false; qfalse when absent
static qboolean RP_LuaWorld_OptBool( lua_State *L, int idx, const char *key )
{
	qboolean v;

	if ( !RP_LuaWorld_OptGet( L, idx, key ) )
		return qfalse;
	if ( !lua_isboolean( L, -1 ) )
		luaL_error( L, "%s is true or false", key );
	v = lua_toboolean( L, -1 ) ? qtrue : qfalse;
	lua_pop( L, 1 );
	return v;
}

// opts.<key>: a word, copied into out; qfalse when absent
static qboolean RP_LuaWorld_OptWord( lua_State *L, int idx, const char *key, char *out, int outSize, const char *takes )
{
	if ( !RP_LuaWorld_OptGet( L, idx, key ) )
		return qfalse;
	if ( lua_type( L, -1 ) != LUA_TSTRING || !lua_tostring( L, -1 )[0] || strlen( lua_tostring( L, -1 ) ) >= (size_t)outSize )
		luaL_error( L, "%s is %s", key, takes );
	Q_strncpyz( out, lua_tostring( L, -1 ), outSize );
	lua_pop( L, 1 );
	return qtrue;
}

// opts.team: the words /npc team and "npcteam" take; -1 when absent (RP_NpcTeamFromName(), NPC_spawn.c)
static int RP_LuaWorld_OptTeam( lua_State *L, int idx )
{
	char word[64];
	int team;

	if ( !RP_LuaWorld_OptWord( L, idx, "team", word, sizeof( word ), "player, enemy, neutral or free" ) )
		return -1;
	team = RP_NpcTeamFromName( word );
	if ( team < 0 )
		luaL_error( L, "team is player, enemy, neutral or free" );
	return team;
}

// opts.effect: the words /npc effect and "npceffect" take; RP_PHASE_NONE when absent (RP_NpcEffectFromName())
static int RP_LuaWorld_OptEffect( lua_State *L, int idx )
{
	char word[64];
	int effect;

	if ( !RP_LuaWorld_OptWord( L, idx, "effect", word, sizeof( word ), "holo, ghost or walkthrough" ) )
		return RP_PHASE_NONE;
	effect = RP_NpcEffectFromName( word );
	if ( effect <= RP_PHASE_NONE )
		luaL_error( L, "effect is holo, ghost or walkthrough" );
	return effect;
}

// opts.hat / opts.cape: a name from that cosmetic list, or "none"; 0 for none or absent (BG_RpCosmeticId(), as
// /entadd checks "npchat" and "npccape")
static int RP_LuaWorld_OptCosmetic( lua_State *L, int idx, const char *key, int slot )
{
	char word[64];
	int id;

	if ( !RP_LuaWorld_OptWord( L, idx, key, word, sizeof( word ), "a name from its list, or \"none\"" ) )
		return 0;
	if ( !Q_stricmp( word, "none" ) )
		return 0;
	id = BG_RpCosmeticId( slot, word );
	if ( !id )
		luaL_error( L, "%s %s is not on the %s list", key, word, key );
	return id;
}

// the options a vehicle takes none of, as /entadd refuses those keys on an NPC_Vehicle
static void RP_LuaWorld_OptNotForVehicle( lua_State *L, int idx )
{
	static const char *const keys[] = { "health", "effect", "frozen", "noai", "hat", "cape", "credits", "xp", NULL };
	int i;

	for ( i = 0; keys[i]; i++ )
	{
		if ( RP_LuaWorld_OptGet( L, idx, keys[i] ) )
			luaL_error( L, "%s is not for a vehicle", keys[i] );
	}
}

// how long a <name> may be for this plugin: lua.<plugin>.<name> has to fit a targetname (MAX_QPATH)
static int RP_LuaWorld_NameRoom( void )
{
	int room = MAX_QPATH - 1 - 4 - (int)strlen( rpLuaCurrent->name ) - 1;

	return room < RP_LUA_ENT_NAME_MAX ? room : RP_LUA_ENT_NAME_MAX;
}

// "lua.<plugin>.<name>" into out; qfalse when it would not fit
static qboolean RP_LuaWorld_PrefixedName( const char *name, char *out, int outSize )
{
	if ( (int)strlen( name ) > RP_LuaWorld_NameRoom() )
		return qfalse;
	Com_sprintf( out, outSize, "lua.%s.%s", rpLuaCurrent->name, name );
	return qtrue;
}

// opts.name -> "lua.<plugin>.<name>" in out, or out empty when there is none; an error for a bad one
static void RP_LuaWorld_Targetname( lua_State *L, int idx, char *out, int outSize )
{
	const char *name;

	out[0] = '\0';
	if ( lua_isnoneornil( L, idx ) )
		return;
	luaL_checktype( L, idx, LUA_TTABLE );
	RP_LuaWorld_RawField( L, idx, "name" );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	name = lua_tostring( L, -1 );
	if ( !name || !RP_LuaWorld_NameOk( name ) || !RP_LuaWorld_PrefixedName( name, out, outSize ) )
		luaL_error( L, "name is letters, digits and '_', up to %d", RP_LuaWorld_NameRoom() );
	lua_pop( L, 1 );
}

/*
==================
World.use( targetname, player )

Every entity with that targetname, through GlobalUse() with the player as activator: the lock check with
the player's account (no bypass, no pass mark), the stock depth and chain limits, the map's own chain
after it. NPCs and vehicles (using one boards it), held entities, those without a use and target_luas (a script may
not load plugins) are skipped, as /entuse skips the first ones. The entity_used event does not fire for a use made by a script (rpLuaCurrent).
A chain may reach a target_reward or a target_dialog: it pays or opens for the player, as for a button --
how much and how often is the server host's design. Every use takes room in the player's allowance
(RP_LUA_SENDS_PER_SEC, shared with Msg.*), and what the chain sends that player, or writes to the database,
is counted by the stability guards (rp_lua_api.c).
==================
*/
static int RP_LuaWorld_Use( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	static int found[MAX_ENTITIESTOTAL];
	const char *name = luaL_checkstring( L, 1 );
	gentity_t *player, *e = NULL;
	int numFound = 0, used = 0, skipped = 0, i, clientNum;

	if ( !name[0] || strlen( name ) >= MAX_QPATH )
		return luaL_error( L, "a targetname is 1 to %d characters", MAX_QPATH - 1 );
	RP_LuaApi_PlayerArg( L, 2, &clientNum );
	player = &g_entities[clientNum];

	while ( ( e = G_Find( e, FOFS( targetname ), name ) ) != NULL && numFound < MAX_ENTITIESTOTAL )
		found[numFound++] = e - g_entities;
	if ( !numFound )
		return RP_LuaWorld_Refuse( L, "nothing has that targetname" );
	if ( !RP_LuaApi_SendRoom( clientNum ) )
	{
		p->usesBusy++;
		return RP_LuaWorld_Refuse( L, "player busy" );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	RP_LuaApi_Sent( clientNum );

	for ( i = 0; i < numFound; i++ )
	{
		e = &g_entities[found[i]];
		if ( !e->inuse || !e->targetname || Q_stricmp( e->targetname, name ) )
			continue;	// gone, or another entity now, since the search
		if ( RP_EntityIsCharacter( e ) || e->rpHeldBy || !e->use || ( e->classname && !Q_stricmp( e->classname, "target_lua" ) ) )
		{ // a target_lua loads plugins: people choose those (rp_lua_plugins, /scripts, the entity itself), never a script
			skipped++;
			continue;
		}
		GlobalUse( e, player, player );
		used++;
	}
	p->uses++;
	lua_pushinteger( L, used );
	lua_pushinteger( L, skipped );
	return 2;
}

/*
==================
World.prop( classname, origin, angles, keys, opts )

The /entadd sequence (zyk_entadd, g_cmds.c) for one of the Entity System's exempt classes (RP_MapExemptClass:
pickups, dispensers, decor, fx_runner, target_speaker, and the quest entities target_dialog, target_reward,
target_flag, target_play_music -- the classes /entedit and /entremove may touch even when the map made them).
The keys are validated as /entadd validates them, with the name keys, rpLock and the spawner reward keys
refused, and every link key (target..., a dialogue's go<n> and speaker) taken as a plain name of one of the
plugin's own entities and stored with the plugin's prefix; the game's spawn function decides the rest, as it
does for /entadd.
==================
*/
static const char *const rpLuaRefusedKeys[] = {
	"classname", "origin", "angles", "aimoffset",	// arguments of the call, not keys
	"targetname", "script_targetname", "npc_targetname", "team",	// the name is opts.name; ICARUS and the mover teams are the map's
	"rpLock", "lock", "npccredits", "npcxp",	// "lock" is the spawn key of rpLock (g_spawn.c): a server lock is the map's
	"targetshadername", "targetshadernewname",	// a shader remap for everyone, a configstring on every firing: the map's business
	NULL
};
// a link key: its value is a plain name of one of this plugin's own entities, stored as lua.<plugin>.<name>, so a
// script's entity can aim only at the plugin's own -- never at the map's, an admin's or another plugin's
static const char *const rpLuaLinkKeys[] = {
	"target", "target2", "target3", "target4", "target5", "target6", "paintarget", "opentarget", "closetarget", "npc_target",
	"npc_target2", "npc_target4", "throwtarget", "goaltarget",	// other spellings of target2/target4 (g_spawn.c, misc_model_breakable's throwtarget)
	"go1", "go2", "go3", "go4", "go5", "go6", "speaker",	// a target_dialog's options and its speaker
	NULL
};

typedef struct {
	char	key[RP_LUA_KEY_NAME_MAX + 1];
	char	value[RP_LUA_KEY_VALUE_MAX + 1];
} rpLuaPair_t;

static qboolean RP_LuaWorld_KeyOk( const char *key )
{
	int i;

	if ( !key[0] || !( ( key[0] >= 'a' && key[0] <= 'z' ) || ( key[0] >= 'A' && key[0] <= 'Z' ) || key[0] == '_' ) )
		return qfalse;
	for ( i = 0; key[i]; i++ )
	{
		char c = key[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
			return qfalse;
	}
	return qtrue;
}

static qboolean RP_LuaWorld_KeyRefused( const char *key )
{
	int i;

	for ( i = 0; rpLuaRefusedKeys[i]; i++ )
	{
		if ( !Q_stricmp( key, rpLuaRefusedKeys[i] ) )
			return qtrue;
	}
	return qfalse;
}

static qboolean RP_LuaWorld_KeyIsLink( const char *key )
{
	int i;

	for ( i = 0; rpLuaLinkKeys[i]; i++ )
	{
		if ( !Q_stricmp( key, rpLuaLinkKeys[i] ) )
			return qtrue;
	}
	return qfalse;
}

// keys table at idx -> pairs; errors for a bad key, a bad value or too many
static int RP_LuaWorld_Keys( lua_State *L, int idx, rpLuaPair_t *pairs, int maxPairs )
{
	int n = 0;

	if ( lua_isnoneornil( L, idx ) )
		return 0;
	luaL_checktype( L, idx, LUA_TTABLE );
	lua_pushnil( L );
	while ( lua_next( L, idx ) )
	{
		const char *key, *value;
		size_t klen, vlen;
		int i;

		if ( lua_type( L, -2 ) != LUA_TSTRING )
			return luaL_error( L, "a key is a string" );
		key = lua_tolstring( L, -2, &klen );
		if ( klen > RP_LUA_KEY_NAME_MAX || !RP_LuaWorld_KeyOk( key ) )
			return luaL_error( L, "'%s' is not a key (letters, digits and '_', up to %d)", key, RP_LUA_KEY_NAME_MAX );
		if ( RP_LuaWorld_KeyRefused( key ) )
			return luaL_error( L, "the key '%s' is not allowed for a script's entity", key );
		if ( lua_type( L, -1 ) != LUA_TSTRING && lua_type( L, -1 ) != LUA_TNUMBER )
			return luaL_error( L, "the value of '%s' is a string or a number", key );
		value = lua_tolstring( L, -1, &vlen );
		if ( !value || vlen > RP_LUA_KEY_VALUE_MAX )
			return luaL_error( L, "the value of '%s' is up to %d characters", key, RP_LUA_KEY_VALUE_MAX );
		for ( i = 0; value[i]; i++ )
		{
			if ( value[i] == '"' || value[i] == '\n' || value[i] == '\r' || value[i] == ';' )
				return luaL_error( L, "the value of '%s' holds a quote, a semicolon or a line break", key );
		}
		if ( RP_EntityVectorKey( key ) )
		{
			vec3_t v;

			if ( sscanf( value, "%f %f %f", &v[0], &v[1], &v[2] ) != 3 || !RP_FINITE( v[0] ) || !RP_FINITE( v[1] ) || !RP_FINITE( v[2] ) )
				return luaL_error( L, "'%s' takes three numbers", key );
		}
		if ( n >= maxPairs )
			return luaL_error( L, "at most %d keys", maxPairs );
		Q_strncpyz( pairs[n].key, key, sizeof( pairs[n].key ) );
		if ( RP_LuaWorld_KeyIsLink( key ) )
		{ // one of the plugin's own entities, by its plain name
			if ( !RP_LuaWorld_NameOk( value ) || !RP_LuaWorld_PrefixedName( value, pairs[n].value, sizeof( pairs[n].value ) ) )
				return luaL_error( L, "'%s' is a name of one of this plugin's entities: letters, digits and '_', up to %d", key, RP_LuaWorld_NameRoom() );
		}
		else
			Q_strncpyz( pairs[n].value, value, sizeof( pairs[n].value ) );
		n++;
		lua_pop( L, 1 );
	}
	return n;
}

static int RP_LuaWorld_Prop( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *classname = luaL_checkstring( L, 1 );
	vec3_t origin, angles;
	rpLuaPair_t pairs[RP_LUA_MAX_KEYS + 4];
	char *ptrs[( RP_LUA_MAX_KEYS + 4 ) * 2];
	char targetname[MAX_QPATH];
	rpSpawnRoute_t route;
	gentity_t *e;
	int n, i;

	if ( !RP_MapExemptClass( classname ) )
		return luaL_error( L, "'%s' is not a class a script may spawn", classname );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	RP_LuaWorld_Angles( L, 3, angles );
	n = RP_LuaWorld_Keys( L, 4, pairs, RP_LUA_MAX_KEYS );
	RP_LuaWorld_Targetname( L, 5, targetname, sizeof( targetname ) );

	// the record: classname, origin, angles, the name, then the keys
	memmove( &pairs[3 + ( targetname[0] ? 1 : 0 )], &pairs[0], n * sizeof( pairs[0] ) );
	Q_strncpyz( pairs[0].key, "classname", sizeof( pairs[0].key ) ); Q_strncpyz( pairs[0].value, classname, sizeof( pairs[0].value ) );
	Q_strncpyz( pairs[1].key, "origin", sizeof( pairs[1].key ) ); Com_sprintf( pairs[1].value, sizeof( pairs[1].value ), "%f %f %f", origin[0], origin[1], origin[2] );
	Q_strncpyz( pairs[2].key, "angles", sizeof( pairs[2].key ) ); Com_sprintf( pairs[2].value, sizeof( pairs[2].value ), "%f %f %f", angles[0], angles[1], angles[2] );
	n += 3;
	if ( targetname[0] )
	{
		Q_strncpyz( pairs[3].key, "targetname", sizeof( pairs[3].key ) ); Q_strncpyz( pairs[3].value, targetname, sizeof( pairs[3].value ) );
		n++;
	}

	if ( RP_LuaWorld_CountLive( p, qfalse ) >= RP_LUA_MAX_PROPS )
		return RP_LuaWorld_Refuse( L, "too many props" );
	if ( p->propSpawns >= RP_LUA_PROP_SPAWNS_MAP )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );
	if ( !Q_stricmp( classname, "target_dialog" ) && p->dialogSpawns >= RP_LUA_DIALOG_SPAWNS_MAP )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );	// each keeps ~1.5 KB of pool to the map's end
	RP_SpawnRouteInit( &route );
	for ( i = 0; i < n; i++ )
	{
		RP_SpawnRouteNoteKey( &route, pairs[i].key, pairs[i].value );
		ptrs[i * 2] = pairs[i].key;
		ptrs[i * 2 + 1] = pairs[i].value;
	}
	if ( !RP_SpawnRouteHasRoom( &route ) || !G_AllocRoomFor( RP_PairsBytes( ptrs, n * 2 ) ) )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	if ( p->poolBytes + RP_PairsBytes( ptrs, n * 2 ) > RP_LUA_POOL_KB_MAP * 1024 )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );	// the plugin's pool budget (RP_Lua_PoolRoom), by name: a reload keeps it
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	RP_Lua_PoolRoom( p, RP_PairsBytes( ptrs, n * 2 ) );
	p->propSpawns++;
	if ( !Q_stricmp( classname, "target_dialog" ) )
		p->dialogSpawns++;

	e = RP_SpawnForRoute( &route );
	if ( !e )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	for ( i = 0; i < n; i++ )
		zyk_main_set_entity_field( e, pairs[i].key, pairs[i].value );
	RP_LuaWorld_Tag( p, e, qfalse, qfalse );
	level.rp_spawn_refusal[0] = '\0';
	zyk_main_spawn_entity( e );
	if ( !e->inuse )
	{
		p->spawnsRefused++;
		RP_LuaWorld_Compact( p );
		return RP_LuaWorld_Refuse( L, level.rp_spawn_refusal[0] ? level.rp_spawn_refusal : "the spawn did not survive" );
	}
	RP_LuaWorld_PushEntity( L, e );
	return 1;
}

/*
==================
World.npc( type, origin, angles, opts )

A spawner built the way G_VehicleSpawn() and NPC_SpawnType() build one, at the origin given, then
NPC_Spawn_Do() with its own checks (three free slots past the reserve, RP_NPC_MAX_LIVE). No orders, no
team, no effect: the NPC does what its type does. Vehicles are refused.
==================
*/
static int RP_LuaWorld_Npc( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *type = luaL_checkstring( L, 1 );
	vec3_t origin, angles;
	char targetname[MAX_QPATH];
	gentity_t *spawner, *npc;
	int credits, xp, health, team, effect, scale, hat, cape;
	qboolean frozen, noai;

	if ( !type[0] || strlen( type ) >= 64 )
		return luaL_error( L, "an NPC type is 1 to 63 characters" );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	RP_LuaWorld_Angles( L, 3, angles );
	RP_LuaWorld_Targetname( L, 4, targetname, sizeof( targetname ) );
	credits = RP_LuaWorld_OptInt( L, 4, "credits", 0, RP_REWARD_MAX_CREDITS, 0 );
	xp = RP_LuaWorld_OptInt( L, 4, "xp", 0, RP_REWARD_MAX_XP, 0 );
	health = RP_LuaWorld_OptInt( L, 4, "health", 1, RP_LUA_NPC_HEALTH_MAX, 0 );
	team = RP_LuaWorld_OptTeam( L, 4 );
	effect = RP_LuaWorld_OptEffect( L, 4 );
	frozen = RP_LuaWorld_OptBool( L, 4, "frozen" );
	noai = RP_LuaWorld_OptBool( L, 4, "noai" );
	scale = RP_LuaWorld_OptInt( L, 4, "scale", RP_NPC_SCALE_MIN, RP_NPC_SCALE_MAX, 0 );
	hat = RP_LuaWorld_OptCosmetic( L, 4, "hat", RP_COSMETIC_HAT );
	cape = RP_LuaWorld_OptCosmetic( L, 4, "cape", RP_COSMETIC_CAPE );
	if ( !RP_NpcTypeKnown( type ) )
		return RP_LuaWorld_Refuse( L, "unknown NPC type" );
	if ( RP_NpcTypeIsVehicle( type ) )
		return RP_LuaWorld_Refuse( L, "a vehicle type: use World.vehicle" );
	if ( RP_LuaWorld_CountLive( p, qtrue ) >= RP_LUA_MAX_NPCS )
		return RP_LuaWorld_Refuse( L, "too many npcs" );
	if ( p->npcSpawns >= RP_LUA_NPC_SPAWNS_MAP || p->poolBytes + RP_LUA_NPC_POOL_BYTES > RP_LUA_POOL_KB_MAP * 1024 )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );
	if ( !G_EntitySlotsAvailable( 3 ) || !G_AllocRoomFor( RP_LUA_NPC_POOL_BYTES ) )
	{ // the spawner's strings and the NPC's own (NPC_type, the name) are level pool, never freed within a map
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	RP_Lua_PoolRoom( p, RP_LUA_NPC_POOL_BYTES );
	p->npcSpawns++;

	spawner = RP_SpawnForClassname( "npc_spawner", qfalse, qfalse );
	spawner->think = G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;
	G_SetOrigin( spawner, origin );
	VectorCopy( spawner->r.currentOrigin, spawner->s.origin );
	spawner->s.angles[YAW] = angles[YAW];
	if ( !spawner->isLogical )
		trap->LinkEntity( (sharedEntity_t *)spawner );
	spawner->NPC_type = G_NewString( type );
	if ( targetname[0] )
		spawner->NPC_targetname = G_NewString( targetname );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->rpRewardCredits = credits;	// NPC_Spawn_Do copies them to the NPC; player_die pays its killer
	spawner->rpRewardXP = xp;
	// the npc_spawner keys, as SP_NPC_spawner reads them: health, npcteam, npceffect, npcfreeze, the CINEMATIC
	// spawnflag, npcscale, npchat, npccape -- NPC_Spawn_Do and NPC_Begin apply them as for any spawner
	spawner->health = health;
	spawner->rpSpawnerTeam = ( team >= 0 ) ? team + 1 : 0;
	spawner->rpSpawnerEffect = effect;
	spawner->rpSpawnerFreeze = frozen;
	if ( noai )
		spawner->spawnflags |= SFB_CINEMATIC;
	spawner->rpNpcScale = scale;
	spawner->rpNpcCosmetic[RP_COSMETIC_HAT] = hat;
	spawner->rpNpcCosmetic[RP_COSMETIC_CAPE] = cape;
	RP_NpcPrecacheByType( type );

	npc = NPC_Spawn_Do( spawner );
	if ( !npc )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	RP_LuaWorld_Tag( p, npc, qtrue, qfalse );
	RP_LuaWorld_PushEntity( L, npc );
	return 1;
}

/*
==================
World.vehicle( type, origin, angles, opts )

A spawner built as a map's NPC_Vehicle is (its classname is what makes NPC_Spawn_Do() build a vehicle), then
what G_VehicleSpawn() does after: the yaw, and BS_CINEMATIC for all but animals, so it stands still until
boarded. NPC_Spawn_Do() keeps its checks (three free slots past the reserve, RP_NPC_MAX_LIVE) and frees the
spawner itself for a type with no vehicle file. The droid a vehicle brings spawns through the same checks,
or not at all. A script never boards, drives or empties a vehicle.
==================
*/
static int RP_LuaWorld_Vehicle( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *type = luaL_checkstring( L, 1 );
	vec3_t origin, angles;
	char targetname[MAX_QPATH];
	gentity_t *spawner, *veh;
	int team, scale;

	if ( !type[0] || strlen( type ) >= 64 )
		return luaL_error( L, "a vehicle type is 1 to 63 characters" );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	RP_LuaWorld_Angles( L, 3, angles );
	RP_LuaWorld_Targetname( L, 4, targetname, sizeof( targetname ) );
	RP_LuaWorld_OptNotForVehicle( L, 4 );
	team = RP_LuaWorld_OptTeam( L, 4 );
	scale = RP_LuaWorld_OptInt( L, 4, "scale", RP_NPC_SCALE_MIN, RP_NPC_SCALE_MAX, 0 );
	if ( !RP_NpcTypeKnown( type ) || !RP_NpcTypeIsVehicle( type ) )
		return RP_LuaWorld_Refuse( L, "not a vehicle type" );
	if ( RP_LuaWorld_CountVehicles( p ) >= RP_LUA_MAX_VEHICLES )
		return RP_LuaWorld_Refuse( L, "too many vehicles" );
	if ( p->vehicleSpawns >= RP_LUA_VEHICLE_SPAWNS_MAP || p->poolBytes + RP_LUA_NPC_POOL_BYTES > RP_LUA_POOL_KB_MAP * 1024 )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );
	if ( RP_LuaWorld_VehiclesOnServer() >= RP_LUA_VEHICLES_SERVER )
		return RP_LuaWorld_Refuse( L, "too many vehicles on the server" );
	if ( !G_EntitySlotsAvailable( 3 ) || !G_AllocRoomFor( RP_LUA_NPC_POOL_BYTES ) )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	RP_Lua_PoolRoom( p, RP_LUA_NPC_POOL_BYTES );
	p->vehicleSpawns++;

	spawner = RP_SpawnForClassname( "npc_spawner", qfalse, qfalse );
	spawner->classname = "NPC_Vehicle";	// as NPC_SpawnType() does for /npc spawn vehicle
	spawner->think = G_FreeEntity;
	spawner->nextthink = level.time + FRAMETIME;
	G_SetOrigin( spawner, origin );
	VectorCopy( spawner->r.currentOrigin, spawner->s.origin );
	spawner->s.angles[YAW] = angles[YAW];
	if ( !spawner->isLogical )
		trap->LinkEntity( (sharedEntity_t *)spawner );
	spawner->NPC_type = G_NewString( type );
	if ( targetname[0] )
		spawner->NPC_targetname = G_NewString( targetname );
	spawner->count = 1;
	spawner->delay = 0;
	spawner->rpSpawnerTeam = ( team >= 0 ) ? team + 1 : 0;	// "npcteam" and "npcscale", as SP_NPC_Vehicle reads them
	spawner->rpNpcScale = scale;

	veh = NPC_Spawn_Do( spawner );
	if ( !veh )
	{ // no room, or the type has no vehicle file (NPC_Spawn_Do freed the spawner then)
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "the spawn failed (no room, or no vehicle file for that type)" );
	}
	// G_VehicleSpawn()'s tail
	veh->s.angles[YAW] = angles[YAW];
	if ( veh->NPC && veh->m_pVehicle && veh->m_pVehicle->m_pVehicleInfo && veh->m_pVehicle->m_pVehicleInfo->type != VH_ANIMAL )
		veh->NPC->behaviorState = BS_CINEMATIC;
	RP_LuaWorld_Tag( p, veh, qtrue, qtrue );
	RP_LuaWorld_PushEntity( L, veh );
	return 1;
}

/*
==================
World.remove( handle )
==================
*/
static int RP_LuaWorld_Remove( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	gentity_t *ent = RP_LuaWorld_Handle( L, 1 );
	int i;

	if ( !ent )
		return RP_LuaWorld_Refuse( L, "not a live entity of this plugin" );
	if ( RP_LuaWorld_Ridden( ent ) )
		return RP_LuaWorld_Refuse( L, "someone is riding it" );
	for ( i = 0; i < p->numEnts; i++ )
	{
		if ( p->ents[i].number == ent - g_entities && p->ents[i].stamp == ent->rpLuaStamp )
		{
			p->ents[i].removeQueued = qtrue;
			p->removesQueued = qtrue;
			lua_pushboolean( L, 1 );
			return 1;
		}
	}
	return RP_LuaWorld_Refuse( L, "not a live entity of this plugin" );
}

/*
==================
Effect.play( name, origin, angles ), Sound.play( name, origin )

Names already in the configstrings only (RP_ConfigstringIndexOf never registers one), and a free slot for
the temp entity first: G_TempEntity() does not check (G_Spawn ends the server when the table is full).
==================
*/
static int RP_LuaWorld_EffectPlay( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *name = luaL_checkstring( L, 1 );
	vec3_t origin, angles;
	int idx;

	if ( !name[0] || strlen( name ) >= MAX_QPATH )
		return luaL_error( L, "an effect name is 1 to %d characters", MAX_QPATH - 1 );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	RP_LuaWorld_Angles( L, 3, angles );
	idx = RP_ConfigstringIndexOf( name, CS_EFFECTS, MAX_FX );
	if ( !idx )
		return RP_LuaWorld_Refuse( L, "that effect is not registered on this map" );
	if ( !G_EntitySlotsAvailable( 1 ) )
		return RP_LuaWorld_Refuse( L, "no room" );
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	G_PlayEffectID( idx, origin, angles );
	p->effects++;
	lua_pushboolean( L, 1 );
	return 1;
}

static int RP_LuaWorld_SoundPlay( lua_State *L )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *name = luaL_checkstring( L, 1 );
	vec3_t origin;
	int idx;

	if ( !name[0] || strlen( name ) >= MAX_QPATH )
		return luaL_error( L, "a sound name is 1 to %d characters", MAX_QPATH - 1 );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	idx = RP_ConfigstringIndexOf( name, CS_SOUNDS, MAX_SOUNDS );
	if ( !idx )
		return RP_LuaWorld_Refuse( L, "that sound is not registered on this map" );
	if ( !G_EntitySlotsAvailable( 1 ) )
		return RP_LuaWorld_Refuse( L, "no room" );
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	G_SoundAtLoc( origin, CHAN_AUTO, idx );
	p->sounds++;
	lua_pushboolean( L, 1 );
	return 1;
}

/*
==================
Opening
==================
*/
static const luaL_Reg rpLuaEntityMeta[] = {
	{ "__index",	RP_LuaWorld_EntityIndex },
	{ "__eq",		RP_LuaWorld_EntityEq },
	{ "__tostring",	RP_LuaWorld_EntityToString },
	{ NULL, NULL }
};
static const luaL_Reg rpLuaWorldP2[] = {
	{ "use", RP_LuaWorld_Use }, { "prop", RP_LuaWorld_Prop }, { "npc", RP_LuaWorld_Npc }, { "vehicle", RP_LuaWorld_Vehicle },
	{ "remove", RP_LuaWorld_Remove }, { NULL, NULL }
};
static const luaL_Reg rpLuaEffect[] = { { "play", RP_LuaWorld_EffectPlay }, { NULL, NULL } };
static const luaL_Reg rpLuaSound[] = { { "play", RP_LuaWorld_SoundPlay }, { NULL, NULL } };

void RP_LuaWorld_OpenLib( rpLuaPlugin_t *p )
{
	lua_State *L = p->L;

	luaL_newmetatable( L, RP_LUA_ENTITY_MT );
	luaL_setfuncs( L, rpLuaEntityMeta, 0 );
	lua_pushboolean( L, 0 );
	lua_setfield( L, -2, "__metatable" );
	lua_pop( L, 1 );

	lua_getglobal( L, "World" );	// the P1 table, extended
	luaL_setfuncs( L, rpLuaWorldP2, 0 );
	lua_pop( L, 1 );
	lua_newtable( L );
	luaL_setfuncs( L, rpLuaEffect, 0 );
	lua_setglobal( L, "Effect" );
	lua_newtable( L );
	luaL_setfuncs( L, rpLuaSound, 0 );
	lua_setglobal( L, "Sound" );
	lua_pushinteger( L, 2 );
	lua_setglobal( L, "API_VERSION" );
}
