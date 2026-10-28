/*
===========================================================================
GLX_U: [Lua] P2 -- the world, as a player reaches it. Every call here is an existing game function a
player or an admin command already runs, with that function's own checks, and a token from the same
bucket the messages use: a plugin acts at a player's pace.

  World.use( targetname, player )         GlobalUse() on each entity of that name, the player as activator:
                                          locks, depth and chain limits as for a button; no admin bypass
  World.prop( class, origin, angles, keys ) the /entadd sequence, for the Entity System's exempt classes
  World.npc( type, origin, angles, opts ) a spawner and NPC_Spawn_Do(), as /npc spawn
  World.remove( handle )                  queued: freed at the end of the frame, never inside a call
  Effect.play( name, origin, angles )     G_PlayEffectID() of an effect already registered
  Sound.play( name, origin )              G_SoundAtLoc() of a sound already registered

A spawned entity carries the plugin's slot and a stamp (gentity_t::rpLuaPlugin, rpLuaStamp): /entsave leaves
it out, a handle to a reused slot never matches, and unload, reload, disable and the map's end take every
entity the plugin made (RP_LuaWorld_PluginClose, from RP_Lua_ClosePlugin, which runs at a frame end only).
===========================================================================
*/
#include "rp_lua_local.h"

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
		if ( p->ents[i].npc == npcs )
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

static void RP_LuaWorld_Tag( rpLuaPlugin_t *p, gentity_t *ent, qboolean npc )
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
	e->removeQueued = qfalse;
}

// the one place an entity a script made is freed: at a frame end (RP_LuaWorld_Frame, RP_Lua_RunQueue) or
// at the map's end (RP_Lua_Shutdown) -- never inside a script call
static void RP_LuaWorld_FreeNow( rpLuaEntity_t *e )
{
	gentity_t *ent;

	if ( !RP_LuaWorld_Live( e ) )
		return;
	ent = &g_entities[e->number];
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
			RP_LuaWorld_FreeNow( &p->ents[i] );
	}
	p->removesQueued = qfalse;
	RP_LuaWorld_Compact( p );
}

void RP_LuaWorld_PluginClose( rpLuaPlugin_t *p )
{
	int i;

	for ( i = 0; i < p->numEnts; i++ )
		RP_LuaWorld_FreeNow( &p->ents[i] );
	p->numEnts = 0;
	p->removesQueued = qfalse;
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
static void RP_LuaWorld_Vec( lua_State *L, int idx, vec3_t out, const char *what )
{
	static const char *const names[3] = { "x", "y", "z" };
	int i;

	if ( !lua_istable( L, idx ) )
		luaL_error( L, "%s is a table {x=, y=, z=}", what );
	for ( i = 0; i < 3; i++ )
	{
		lua_getfield( L, idx, names[i] );
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

// opts.name -> "lua.<plugin>.<name>" in out, or out empty when there is none; an error for a bad one
static void RP_LuaWorld_Targetname( lua_State *L, int idx, char *out, int outSize )
{
	const char *name;

	out[0] = '\0';
	if ( lua_isnoneornil( L, idx ) )
		return;
	luaL_checktype( L, idx, LUA_TTABLE );
	lua_getfield( L, idx, "name" );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	name = lua_tostring( L, -1 );
	if ( !name || !RP_LuaWorld_NameOk( name ) )
		luaL_error( L, "name is letters, digits and '_', up to %d", RP_LUA_ENT_NAME_MAX );
	Com_sprintf( out, outSize, "lua.%s.%s", rpLuaCurrent->name, name );
	lua_pop( L, 1 );
}

/*
==================
World.use( targetname, player )

Every entity with that targetname, through GlobalUse() with the player as activator: the lock check with
the player's account (no bypass, no pass mark), the stock depth and chain limits, the map's own chain
after it. NPCs and vehicles (using one boards it), held entities and those without a use are skipped, as
/entuse skips them. The entity_used event does not fire for a use made by a script (rpLuaCurrent).
==================
*/
// GLX_U: [Rewards][Dialogues] a target_reward or a target_dialog reachable from these entities through the
// keys a use follows (target, target2..6, a mover's open/close/pain targets, and a dialogue's go1..6): a
// script may not pull a trigger that pays anyone or starts a conversation, however many relays, delays or
// counters sit in between. A bounded walk with a visited set, over the networked and the logical region
// both (G_Find searches both). The refusal text, or NULL.
static const char *RP_LuaWorld_ChainRefused( const int *start, int numStart )
{
	static byte visited[MAX_ENTITIESTOTAL];
	static int queue[MAX_ENTITIESTOTAL];
	int head = 0, tail = 0, steps = 0, i;

	memset( visited, 0, sizeof( visited ) );
	for ( i = 0; i < numStart; i++ )
	{
		if ( start[i] >= 0 && start[i] < MAX_ENTITIESTOTAL && !visited[start[i]] )
		{
			visited[start[i]] = 1;
			queue[tail++] = start[i];
		}
	}
	while ( head < tail && steps++ < 2048 )
	{
		gentity_t *e = &g_entities[queue[head++]];
		const char *names[9 + RP_DIALOG_OPTIONS];
		int n, count = 9;

		if ( !e->inuse )
			continue;
		if ( e->classname && !Q_stricmp( e->classname, "target_reward" ) )
			return "leads to a reward";
		if ( e->classname && !Q_stricmp( e->classname, "target_dialog" ) )
			return "leads to a dialogue";
		names[0] = e->target; names[1] = e->target2; names[2] = e->target3; names[3] = e->target4; names[4] = e->target5;
		names[5] = e->target6; names[6] = e->closetarget; names[7] = e->opentarget; names[8] = e->paintarget;
		count += RP_DialogOptionTargets( e, &names[9], RP_DIALOG_OPTIONS );
		for ( n = 0; n < count; n++ )
		{
			gentity_t *t = NULL;

			if ( !names[n] || !names[n][0] )
				continue;
			while ( ( t = G_Find( t, FOFS( targetname ), names[n] ) ) != NULL && tail < MAX_ENTITIESTOTAL )
			{
				int num = t - g_entities;

				if ( num >= 0 && num < MAX_ENTITIESTOTAL && !visited[num] )
				{
					visited[num] = 1;
					queue[tail++] = num;
				}
			}
		}
	}
	return NULL;
}

static int RP_LuaWorld_Use( lua_State *L )
{
	rpLuaPlugin_t *p = rpLuaCurrent;
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
	{
		const char *refused = RP_LuaWorld_ChainRefused( found, numFound );

		if ( refused )
			return RP_LuaWorld_Refuse( L, refused );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );

	for ( i = 0; i < numFound; i++ )
	{
		e = &g_entities[found[i]];
		if ( !e->inuse || !e->targetname || Q_stricmp( e->targetname, name ) )
			continue;	// gone, or another entity now, since the search
		if ( RP_EntityIsCharacter( e ) || e->rpHeldBy || !e->use )
		{
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
World.prop( classname, origin, angles, keys )

The /entadd sequence (zyk_entadd, g_cmds.c) for one of the Entity System's exempt classes (RP_MapExemptClass:
pickups, dispensers, decor, fx_runner, target_speaker -- the classes /entedit and /entremove may touch even
when the map made them). The keys are validated as /entadd validates them, with the link keys, rpLock and the
spawner reward keys refused; the game's spawn function decides the rest, as it does for /entadd.
==================
*/
static const char *const rpLuaRefusedKeys[] = {
	"classname", "origin", "angles", "aimoffset",	// arguments of the call, not keys
	"targetname", "script_targetname", "team", "target", "target2", "target3", "target4", "target5", "target6",	// links to the map
	"rpLock", "npccredits", "npcxp",
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
		Q_strncpyz( pairs[n].value, value, sizeof( pairs[n].value ) );
		n++;
		lua_pop( L, 1 );
	}
	return n;
}

static int RP_LuaWorld_Prop( lua_State *L )
{
	rpLuaPlugin_t *p = rpLuaCurrent;
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
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
	p->propSpawns++;

	e = RP_SpawnForRoute( &route );
	if ( !e )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	for ( i = 0; i < n; i++ )
		zyk_main_set_entity_field( e, pairs[i].key, pairs[i].value );
	RP_LuaWorld_Tag( p, e, qfalse );
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
	rpLuaPlugin_t *p = rpLuaCurrent;
	const char *type = luaL_checkstring( L, 1 );
	vec3_t origin, angles;
	char targetname[MAX_QPATH];
	gentity_t *spawner, *npc;

	if ( !type[0] || strlen( type ) >= 64 )
		return luaL_error( L, "an NPC type is 1 to 63 characters" );
	RP_LuaWorld_Vec( L, 2, origin, "origin" );
	RP_LuaWorld_Angles( L, 3, angles );
	RP_LuaWorld_Targetname( L, 4, targetname, sizeof( targetname ) );
	if ( !RP_NpcTypeKnown( type ) )
		return RP_LuaWorld_Refuse( L, "unknown NPC type" );
	if ( RP_NpcTypeIsVehicle( type ) )
		return RP_LuaWorld_Refuse( L, "vehicles cannot be spawned by a script" );
	if ( RP_LuaWorld_CountLive( p, qtrue ) >= RP_LUA_MAX_NPCS )
		return RP_LuaWorld_Refuse( L, "too many npcs" );
	if ( p->npcSpawns >= RP_LUA_NPC_SPAWNS_MAP )
		return RP_LuaWorld_Refuse( L, "spawn limit for this map" );
	if ( !G_EntitySlotsAvailable( 3 ) )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	if ( !RP_LuaApi_TakeToken( p, 1 ) )
		return RP_LuaWorld_Refuse( L, "too fast" );
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
	RP_NpcPrecacheByType( type );

	npc = NPC_Spawn_Do( spawner );
	if ( !npc )
	{
		p->spawnsRefused++;
		return RP_LuaWorld_Refuse( L, "no room" );
	}
	RP_LuaWorld_Tag( p, npc, qtrue );
	RP_LuaWorld_PushEntity( L, npc );
	return 1;
}

/*
==================
World.remove( handle )
==================
*/
static int RP_LuaWorld_Remove( lua_State *L )
{
	rpLuaPlugin_t *p = rpLuaCurrent;
	gentity_t *ent = RP_LuaWorld_Handle( L, 1 );
	int i;

	if ( !ent )
		return RP_LuaWorld_Refuse( L, "not a live entity of this plugin" );
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
	rpLuaPlugin_t *p = rpLuaCurrent;
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
	rpLuaPlugin_t *p = rpLuaCurrent;
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
	{ "use", RP_LuaWorld_Use }, { "prop", RP_LuaWorld_Prop }, { "npc", RP_LuaWorld_Npc }, { "remove", RP_LuaWorld_Remove }, { NULL, NULL }
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
