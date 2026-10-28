/*
===========================================================================
GLX_U: [Lua] the game's events as scripts see them. Every hook builds one table and raises each
plugin's handler for it (RP_Lua_FireEvent); none can change what the game does.
===========================================================================
*/
#include "rp_lua_local.h"

typedef struct {
	gentity_t	*ent, *other;
	int			mode, number;
	const char	*text;
} rpLuaEventCtx_t;

static void RP_Lua_FillPlayer( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;

	RP_LuaApi_PushPlayer( L, c->ent - g_entities );
	lua_setfield( L, -2, "player" );
}

void RP_Lua_EventBegin( gentity_t *ent )
{
	rpLuaEventCtx_t c = { ent, NULL, 0, 0, NULL };

	if ( !ent->client || ( ent->r.svFlags & SVF_BOT ) )
		return;
	RP_Lua_FireEvent( RPLE_CLIENT_BEGIN, RP_Lua_FillPlayer, &c );
}

void RP_Lua_EventDisconnect( gentity_t *ent )
{
	rpLuaEventCtx_t c = { ent, NULL, 0, 0, NULL };

	if ( !ent->client || ( ent->r.svFlags & SVF_BOT ) || ent->client->pers.connected == CON_DISCONNECTED )
		return;
	RP_Lua_FireEvent( RPLE_CLIENT_DISCONNECT, RP_Lua_FillPlayer, &c );
}

static void RP_Lua_FillChat( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;
	const char *mode;
	char text[RP_LUA_TEXT_MAX + 1];

	switch ( c->mode )
	{
	case SAY_TEAM:	mode = "team"; break;
	case SAY_TELL:	mode = "tell"; break;
	case SAY_ALLY:	mode = "ally"; break;
	case SAY_OOC:	mode = "ooc"; break;
	default:		mode = "say"; break;
	}
	RP_LuaApi_PushPlayer( L, c->ent - g_entities );
	lua_setfield( L, -2, "player" );
	lua_pushstring( L, mode );
	lua_setfield( L, -2, "mode" );
	Q_strncpyz( text, c->text ? c->text : "", sizeof( text ) );
	lua_pushstring( L, text );
	lua_setfield( L, -2, "text" );
	if ( c->other && c->other->client )
	{
		RP_LuaApi_PushPlayer( L, c->other - g_entities );
		lua_setfield( L, -2, "target" );
	}
}

void RP_Lua_EventChat( gentity_t *ent, gentity_t *target, int mode, const char *text )
{
	rpLuaEventCtx_t c = { ent, mode == SAY_TELL ? target : NULL, mode, 0, text };

	if ( !ent || !ent->client )
		return;
	RP_Lua_FireEvent( RPLE_CHAT, RP_Lua_FillChat, &c );
}

static void RP_Lua_FillDeath( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;

	RP_LuaApi_PushPlayer( L, c->ent - g_entities );
	lua_setfield( L, -2, "victim" );
	if ( c->other && c->other->client && c->other - g_entities < MAX_CLIENTS )
	{
		RP_LuaApi_PushPlayer( L, c->other - g_entities );
		lua_setfield( L, -2, "killer" );
	}
	lua_pushinteger( L, c->mode );
	lua_setfield( L, -2, "mod" );
}

// P2: npc_death, to the plugin that spawned the NPC and no other
static void RP_Lua_FillNpcDeath( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;

	RP_LuaWorld_PushEntity( L, c->ent );
	lua_setfield( L, -2, "entity" );
	lua_pushstring( L, c->ent->NPC_type ? c->ent->NPC_type : "" );
	lua_setfield( L, -2, "npc_type" );
	if ( c->other && c->other->client && c->other - g_entities < MAX_CLIENTS )
	{
		RP_LuaApi_PushPlayer( L, c->other - g_entities );
		lua_setfield( L, -2, "killer" );
	}
	lua_pushinteger( L, c->mode );
	lua_setfield( L, -2, "mod" );
}

void RP_Lua_EventDeath( gentity_t *victim, gentity_t *attacker, int mod )
{
	rpLuaEventCtx_t c = { victim, attacker, mod, 0, NULL };

	if ( !victim || !victim->client )
		return;
	if ( victim - g_entities >= MAX_CLIENTS )
	{ // NPCs die through player_die too: only one a script spawned is reported, to that script
		rpLuaPlugin_t *p = RP_LuaWorld_OwnerOf( victim );

		if ( !p || !p->L || rpLuaCurrent || p->handlers[RPLE_NPC_DEATH] == LUA_NOREF )
			return;
		lua_rawgeti( p->L, LUA_REGISTRYINDEX, p->handlers[RPLE_NPC_DEATH] );
		lua_newtable( p->L );
		RP_Lua_FillNpcDeath( p->L, &c );
		RP_Lua_Call( p, 1, 0 );
		return;
	}
	RP_Lua_FireEvent( RPLE_PLAYER_DEATH, RP_Lua_FillDeath, &c );
}

static void RP_Lua_FillUsed( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;

	lua_pushstring( L, c->ent->targetname );
	lua_setfield( L, -2, "targetname" );
	lua_newtable( L );
	lua_pushinteger( L, c->ent - g_entities );
	lua_setfield( L, -2, "number" );
	lua_pushstring( L, c->ent->classname ? c->ent->classname : "" );
	lua_setfield( L, -2, "classname" );
	lua_setfield( L, -2, "entity" );
	if ( c->other && c->other->client && c->other - g_entities < MAX_CLIENTS )
	{
		RP_LuaApi_PushPlayer( L, c->other - g_entities );
		lua_setfield( L, -2, "activator" );
	}
}

void RP_Lua_EventUsed( gentity_t *self, gentity_t *activator )
{
	rpLuaEventCtx_t c = { self, activator, 0, 0, NULL };
	int i;

	if ( rpLuaCurrent )
		return;	// P1 scripts cannot use anything, so this cannot happen; kept as the rule
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];
		int w;

		if ( !p->inUse || !p->L || p->handlers[RPLE_ENTITY_USED] == LUA_NOREF )
			continue;
		for ( w = 0; w < p->numWatches; w++ )
		{
			if ( !Q_stricmp( p->watches[w], self->targetname ) )
				break;
		}
		if ( w == p->numWatches )
			continue;
		lua_rawgeti( p->L, LUA_REGISTRYINDEX, p->handlers[RPLE_ENTITY_USED] );
		lua_newtable( p->L );
		RP_Lua_FillUsed( p->L, &c );
		RP_Lua_Call( p, 1, 0 );
	}
}

static void RP_Lua_FillAccount( lua_State *L, void *ctx )
{
	rpLuaEventCtx_t *c = (rpLuaEventCtx_t *)ctx;

	RP_LuaApi_PushPlayer( L, c->ent - g_entities );
	lua_setfield( L, -2, "player" );
	if ( c->ent->client->sess.loggedin )
	{
		lua_pushstring( L, c->ent->client->sess.filename );
		lua_setfield( L, -2, "account" );
	}
	if ( c->text )
	{
		lua_pushinteger( L, c->number );
		lua_setfield( L, -2, c->text );
	}
}

void RP_Lua_EventAccount( gentity_t *ent, const char *event, int number )
{
	rpLuaEventCtx_t c = { ent, NULL, 0, number, NULL };
	rpLuaEvent_t ev;

	if ( !ent || !ent->client )
		return;
	if ( !strcmp( event, "login" ) )
		ev = RPLE_LOGIN;
	else if ( !strcmp( event, "logout" ) )
		ev = RPLE_LOGOUT;
	else if ( !strcmp( event, "character_select" ) )
	{
		ev = RPLE_CHARACTER_SELECT;
		c.text = "char_id";
	}
	else if ( !strcmp( event, "level_up" ) )
	{
		ev = RPLE_LEVEL_UP;
		c.text = "level";
	}
	else
		return;
	RP_Lua_FireEvent( ev, RP_Lua_FillAccount, &c );
}

/*
==================
RP_Lua_Command

ClientCommand's "Unknown command" line asks here first. qtrue when a plugin owns the name.
==================
*/
qboolean RP_Lua_Command( gentity_t *ent, const char *cmd )
{
	int i, k, argc;

	if ( rpLuaCurrent || !ent->client )
		return qfalse;
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse || !p->L )
			continue;
		for ( k = 0; k < p->numCommands; k++ )
		{
			if ( Q_stricmp( p->cmdNames[k], cmd ) )
				continue;
			lua_rawgeti( p->L, LUA_REGISTRYINDEX, p->cmdRefs[k] );
			RP_LuaApi_PushPlayer( p->L, ent - g_entities );
			lua_newtable( p->L );
			argc = trap->Argc();
			if ( argc > RP_LUA_MAX_ARGS + 1 )
				argc = RP_LUA_MAX_ARGS + 1;
			for ( k = 1; k < argc; k++ )
			{
				char arg[MAX_TOKEN_CHARS];

				trap->Argv( k, arg, sizeof( arg ) );
				lua_pushstring( p->L, arg );
				lua_rawseti( p->L, -2, k );
			}
			RP_Lua_Call( p, 2, 0 );
			return qtrue;
		}
	}
	return qfalse;
}
