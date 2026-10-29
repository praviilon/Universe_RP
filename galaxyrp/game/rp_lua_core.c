/*
===========================================================================
GLX_U: [Lua] the states, the loader, the budget, the errors and the end-of-frame queue.

One Lua state per plugin, made with an allocator that refuses past rp_lua_memkb: the whole library is
bounded by that, since no string or table can outgrow it. A call runs under a count hook that checks a
real-clock deadline (RP_LUA_CALL_MS) and an instruction cap, inside lua_pcall, with no nesting: an event
raised while a script call is on the stack (a World.use that pays a level up, or replaces a dialogue) is
not delivered, so a plugin is only ever charged for its own code. Once a call is past its budget every
further instruction raises (killed), so a pcall in the script cannot keep it running. Everything that
is pushed onto a state goes through the protected call (RP_Lua_CallRef): at the memory cap an unprotected
push would end the process. Finalizers (__gc) are refused at setmetatable, so no script code ever runs
outside a call. States are closed in one place, RP_Lua_Frame, never inside a call or a game hook.
===========================================================================
*/
#include "rp_lua_local.h"

#include <stdio.h>

rpLuaPlugin_t rpLuaPlugins[RP_LUA_MAX_PLUGINS];
rpLuaPlugin_t *rpLuaCurrent = NULL;
int rpLuaFrameMsLeft = RP_LUA_FRAME_MS;
int rpLuaGen[MAX_CLIENTS];
int rp_luaActive = 0;
int rp_luaWatchCount = 0;
int rp_luaInCall = 0;

static char rpLuaLoadQueue[RP_LUA_MAX_PLUGINS][RP_LUA_NAME_LEN + 1];
static int rpLuaNumLoadQueued = 0;
static qboolean rpLuaStoreReady = qfalse;
static qboolean rpLuaPanicked = qfalse;	// a Lua panic ended the map: the states are not touched again

// the per-map spawn counters by plugin name: a reload or an unload and load must not start them over, or a
// plugin could take another 128 props' worth of the level pool (never freed within a map) each time
typedef struct {
	char	name[RP_LUA_NAME_LEN + 1];
	int		props, npcs, vehicles, dialogs, poolBytes;
} rpLuaMapSpawns_t;
static rpLuaMapSpawns_t rpLuaMapSpawns[RP_LUA_MAP_NAMES];
static int rpLuaNumMapNames = 0;

// the entry for a name, made when there is none; past RP_LUA_MAP_NAMES names the last entry is shared
static rpLuaMapSpawns_t *RP_Lua_MapSpawnsFor( const char *name )
{
	int i;

	for ( i = 0; i < rpLuaNumMapNames; i++ )
	{
		if ( !Q_stricmp( rpLuaMapSpawns[i].name, name ) )
			return &rpLuaMapSpawns[i];
	}
	if ( rpLuaNumMapNames >= RP_LUA_MAP_NAMES )
		return &rpLuaMapSpawns[RP_LUA_MAP_NAMES - 1];
	memset( &rpLuaMapSpawns[rpLuaNumMapNames], 0, sizeof( rpLuaMapSpawns[0] ) );
	Q_strncpyz( rpLuaMapSpawns[rpLuaNumMapNames].name, name, sizeof( rpLuaMapSpawns[0].name ) );
	return &rpLuaMapSpawns[rpLuaNumMapNames++];
}

static void RP_Lua_MapSpawnsSave( const rpLuaPlugin_t *p )
{
	rpLuaMapSpawns_t *m = RP_Lua_MapSpawnsFor( p->name );

	m->props = p->propSpawns; m->npcs = p->npcSpawns; m->vehicles = p->vehicleSpawns; m->dialogs = p->dialogSpawns; m->poolBytes = p->poolBytes;
}

static void RP_Lua_MapSpawnsRestore( rpLuaPlugin_t *p )
{
	const rpLuaMapSpawns_t *m = RP_Lua_MapSpawnsFor( p->name );

	p->propSpawns = m->props; p->npcSpawns = m->npcs; p->vehicleSpawns = m->vehicles; p->dialogSpawns = m->dialogs; p->poolBytes = m->poolBytes;
}

qboolean RP_Lua_PoolRoom( rpLuaPlugin_t *p, int bytes )
{
	if ( bytes < 0 || p->poolBytes + bytes > RP_LUA_POOL_KB_MAP * 1024 )
		return qfalse;
	p->poolBytes += bytes;
	return qtrue;
}

const char *rpLuaEventNames[RPLE_COUNT] = {
	"init", "shutdown", "frame", "client_begin", "client_disconnect", "chat",
	"player_death", "login", "logout", "character_select", "level_up", "entity_used",
	"npc_death", "dialog_answer"
};

/*
==================
Memory: the one limit that bounds the library
==================
*/
// rp_lua_memkb, clamped; read when a state is made (a change applies to the next load or reload, never to
// a running state: lowering it under a plugin's usage would fail every event in it)
static size_t RP_Lua_MemCap( void )
{
	int kb = rp_lua_memkb.integer < 128 ? 128 : rp_lua_memkb.integer > 4096 ? 4096 : rp_lua_memkb.integer;

	return (size_t)kb * 1024;
}

static void *RP_Lua_Alloc( void *ud, void *ptr, size_t osize, size_t nsize )
{
	rpLuaPlugin_t *p = (rpLuaPlugin_t *)ud;
	size_t cap = p->memCap ? p->memCap : RP_Lua_MemCap();
	size_t old = ptr ? osize : 0;	// with no block, osize is a type tag, not a size

	if ( nsize == 0 )
	{
		free( ptr );
		p->memUsed -= old;
		return NULL;
	}
	if ( nsize > old && p->memUsed - old + nsize > cap )
		return NULL;	// Lua raises "not enough memory" in the running call

	ptr = realloc( ptr, nsize );
	if ( !ptr )
		return NULL;
	p->memUsed = p->memUsed - old + nsize;
	if ( p->memUsed > p->memPeak )
		p->memPeak = p->memUsed;
	return ptr;
}

/*
==================
The count hook: real clock and instruction cap
==================
*/
static void RP_Lua_Hook( lua_State *L, lua_Debug *ar );

// past the budget: from here every instruction raises, so the error climbs out of any pcall in the script
// (each pcall catches one, and the next instruction in the catching frame raises again) up to RP_Lua_Call
static void RP_Lua_Kill( lua_State *L, rpLuaPlugin_t *p, const char *why )
{
	p->killed = qtrue;
	Q_strncpyz( p->killWhy, why, sizeof( p->killWhy ) );
	lua_sethook( L, RP_Lua_Hook, LUA_MASKCOUNT, 1 );
	luaL_error( L, "ran too long (%s)", why );
}

static void RP_Lua_Hook( lua_State *L, lua_Debug *ar )
{
	rpLuaPlugin_t *p = rpLuaCurrent;

	(void)ar;
	if ( !p )
		return;
	if ( p->killed )
		luaL_error( L, "ran too long (%s)", p->killWhy );	// the same error again: a pcall in the script caught the first
	p->instructions += RP_LUA_HOOK_COUNT;
	if ( p->instructions > RP_LUA_CALL_INSTRUCTIONS )
		RP_Lua_Kill( L, p, va( "%d instructions", RP_LUA_CALL_INSTRUCTIONS ) );
	if ( trap->Milliseconds() > p->deadline )
		RP_Lua_Kill( L, p, va( "%d ms", p->deadlineMs ) );
}

// lstrlib.c and ltablib.c (GLX_U), every 1024 steps of a C-side loop the count hook cannot see: pattern
// matching, a plain find, string.rep, table.move
void RP_Lua_LoopCheck( lua_State *L )
{
	rpLuaPlugin_t *p = rpLuaCurrent;

	if ( !p )
		return;
	if ( p->killed )
		luaL_error( L, "ran too long (%s)", p->killWhy );
	if ( trap->Milliseconds() > p->deadline )
		RP_Lua_Kill( L, p, va( "%d ms, in a library loop", p->deadlineMs ) );
}

// an API function reached with no script call on the stack: no path leads here (finalizers are refused), kept as the rule
rpLuaPlugin_t *RP_Lua_NoPlugin( lua_State *L )
{
	luaL_error( L, "not inside a script call" );
	return NULL;
}

// Lua found an error with no protected call to take it: with every push inside RP_Lua_CallRef this never
// runs; if it does, the map ends (G_Error) instead of the process (abort), and the states are left alone
static int RP_Lua_Panic( lua_State *L )
{
	const char *msg = ( lua_type( L, -1 ) == LUA_TSTRING ) ? lua_tostring( L, -1 ) : "(no message)";

	rpLuaPanicked = qtrue;
	G_LogPrintf( "[lua:%s] panic: %s\n", rpLuaCurrent ? rpLuaCurrent->name : "?", msg );
	trap->Error( ERR_DROP, va( "Lua panic: %s", msg ) );
	return 0;
}

static int RP_Lua_MsgHandler( lua_State *L )
{
	const char *msg = lua_tostring( L, 1 );

	if ( !msg )
		msg = "(error object is not a string)";
	luaL_traceback( L, L, msg, 1 );
	return 1;
}

/*
==================
Errors: logged, told to admins now and then, counted; too many switch the plugin off at the frame end
==================
*/
void RP_Lua_Error( rpLuaPlugin_t *p, const char *msg )
{
	int now = level.time, i, recent = 0;
	char line[RP_LUA_TEXT_MAX + 1];

	Q_strncpyz( line, msg, sizeof( line ) );
	G_LogPrintf( "[lua:%s] error: %s\n", p->name, line );
	p->errorsTotal++;
	p->errorTimes[p->errorHead] = now;
	p->errorHead = ( p->errorHead + 1 ) % RP_LUA_MAX_ERRORS;

	if ( now - p->lastNotice >= RP_LUA_NOTICE_MS )
	{
		char *nl = strchr( line, '\n' );

		if ( nl )
			*nl = '\0';	// the first line of the traceback is the message
		RP_LuaAdmin_Notice( p, va( "%s: %s", p->name, line ) );
		p->lastNotice = now;
	}

	for ( i = 0; i < RP_LUA_MAX_ERRORS; i++ )
	{
		if ( p->errorTimes[i] && now - p->errorTimes[i] <= RP_LUA_ERROR_WINDOW_MS )
			recent++;
	}
	if ( recent >= RP_LUA_MAX_ERRORS && p->pendingOp == RPLOP_NONE )
	{
		p->pendingOp = RPLOP_DISABLE;
		G_LogPrintf( "[lua:%s] %d errors in a minute: switched off\n", p->name, recent );
		RP_LuaAdmin_Notice( p, va( "%s: %d errors in a minute, switched off (/scripts enable %s to try again)", p->name, recent, p->name ) );
	}
}

/*
==================
RP_Lua_Call

The function and nargs arguments are on the stack. Runs it under the budget; nresults are left on
success. qfalse when it was refused or ended in an error (nothing is left on the stack then).
==================
*/
// the deadline of an event or command call: RP_LUA_CALL_MS, or RP_LUA_CALL_SHORT_MS once the plugin's or
// every plugin's frame budget is spent (they keep coming on a busy frame; timers wait instead, RP_LuaApi_RunTimers)
int RP_Lua_CallMs( rpLuaPlugin_t *p )
{
	if ( p->frameMsLeft <= 0 || rpLuaFrameMsLeft <= 0 )
	{
		p->eventsShort++;
		return RP_LUA_CALL_SHORT_MS;
	}
	return RP_LUA_CALL_MS;
}

// the function and nargs arguments are on the stack (pushed by the caller, or by the trampoline inside)
static qboolean RP_Lua_Run( rpLuaPlugin_t *p, int nargs, int nresults, int ms )
{
	lua_State *L = p->L;
	int base, status, start;

	base = lua_gettop( L ) - nargs;	// the function's index
	lua_pushcfunction( L, RP_Lua_MsgHandler );
	lua_insert( L, base );

	rpLuaCurrent = p;
	p->inCall = qtrue;
	p->killed = qfalse;
	p->instructions = 0;
	start = trap->Milliseconds();
	p->deadlineMs = ms;
	p->deadline = start + ms;
	lua_sethook( L, RP_Lua_Hook, LUA_MASKCOUNT, RP_LUA_HOOK_COUNT );

	rp_luaInCall++;
	status = lua_pcall( L, nargs, nresults, base );
	rp_luaInCall--;

	lua_sethook( L, NULL, 0, 0 );
	p->killed = qfalse;
	p->inCall = qfalse;
	rpLuaCurrent = NULL;
	{
		int took = trap->Milliseconds() - start;

		p->calls++;
		p->callMs += took;
		p->frameMsLeft -= took;
		rpLuaFrameMsLeft -= took;
	}
	lua_remove( L, base );	// the message handler

	if ( status != LUA_OK )
	{
		RP_Lua_Error( p, lua_tostring( L, -1 ) ? lua_tostring( L, -1 ) : "unknown error" );
		lua_pop( L, 1 );
		return qfalse;
	}
	return qtrue;
}

qboolean RP_Lua_Call( rpLuaPlugin_t *p, int nargs, int nresults )
{
	if ( !p->L )
		return qfalse;
	if ( rpLuaCurrent )
	{ // a nested call: the events raised inside a script call never get here (RP_Lua_FireEvent), so this is a bug in C
		G_LogPrintf( "[lua:%s] nested script call refused\n", p->name );
		lua_pop( p->L, nargs + 1 );
		return qfalse;
	}
	return RP_Lua_Run( p, nargs, nresults, RP_LUA_CALL_MS );
}

// inside the protected call: the function from the registry, its arguments, then the call itself
typedef struct {
	int		ref;
	int		(*push)( lua_State *L, void *ctx );
	void	*ctx;
} rpLuaCallReq_t;

static int RP_Lua_Trampoline( lua_State *L )
{
	rpLuaCallReq_t *r = (rpLuaCallReq_t *)lua_touserdata( L, 1 );
	int n;

	lua_settop( L, 0 );
	lua_rawgeti( L, LUA_REGISTRYINDEX, r->ref );
	n = r->push ? r->push( L, r->ctx ) : 0;
	lua_call( L, n, 0 );
	return 0;
}

qboolean RP_Lua_CallRef( rpLuaPlugin_t *p, int ref, int ms, int (*push)( lua_State *L, void *ctx ), void *ctx )
{
	rpLuaCallReq_t r;

	if ( !p->L || ref == LUA_NOREF )
		return qfalse;
	if ( rpLuaCurrent )
	{
		G_LogPrintf( "[lua:%s] nested script call refused\n", p->name );
		return qfalse;
	}
	r.ref = ref;
	r.push = push;
	r.ctx = ctx;
	lua_pushcfunction( p->L, RP_Lua_Trampoline );	// a light C function and a light userdata: nothing is allocated
	lua_pushlightuserdata( p->L, &r );				// before lua_pcall takes over
	return RP_Lua_Run( p, 1, 0, ms );
}

/*
==================
The sandbox: what a plugin's state offers
==================
*/
static int RP_Lua_Print( lua_State *L )
{
	int n = lua_gettop( L ), i;
	char line[RP_LUA_TEXT_MAX + 1] = { 0 };

	for ( i = 1; i <= n; i++ )
	{
		size_t len;
		const char *s = luaL_tolstring( L, i, &len );

		if ( i > 1 )
			Q_strcat( line, sizeof( line ), "\t" );
		Q_strcat( line, sizeof( line ), s );
		lua_pop( L, 1 );
	}
	G_LogPrintf( "[lua:%s] %s\n", rpLuaCurrent ? rpLuaCurrent->name : "?", line );
	return 0;
}

static int RP_Lua_Load( lua_State *L )
{ // load(chunk [, name]) as text only, a string chunk only
	size_t len;
	const char *chunk = luaL_checklstring( L, 1, &len );
	const char *name = luaL_optstring( L, 2, "=(load)" );

	if ( luaL_loadbufferx( L, chunk, len, name, "t" ) != LUA_OK )
	{
		lua_pushnil( L );
		lua_insert( L, -2 );
		return 2;
	}
	return 1;
}

static int RP_Lua_CollectGarbage( lua_State *L )
{ // only "count"; a script may not drive the collector
	const char *opt = luaL_optstring( L, 1, "count" );

	if ( strcmp( opt, "count" ) != 0 )
		return luaL_error( L, "collectgarbage: only 'count' is allowed" );
	lua_pushnumber( L, (lua_Number)lua_gc( L, LUA_GCCOUNT ) + ( (lua_Number)lua_gc( L, LUA_GCCOUNTB ) / 1024 ) );
	return 1;
}

static int RP_Lua_SetMetatable( lua_State *L )
{ // the stock setmetatable, less __gc: a finalizer runs with the hook off and no call on the stack
	int t = lua_type( L, 2 );

	luaL_checktype( L, 1, LUA_TTABLE );
	luaL_argexpected( L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table" );
	if ( luaL_getmetafield( L, 1, "__metatable" ) != LUA_TNIL )
		return luaL_error( L, "cannot change a protected metatable" );
	if ( t == LUA_TTABLE )
	{
		lua_pushliteral( L, "__gc" );
		lua_rawget( L, 2 );
		if ( !lua_isnil( L, -1 ) )
			return luaL_error( L, "__gc is not allowed" );
		lua_pop( L, 1 );
	}
	lua_settop( L, 2 );
	lua_setmetatable( L, 1 );
	return 1;
}

// xpcall with the handler called after the error unwound, not at the point it was raised: a budget error
// is raised from inside the count hook, where Lua runs the handler with hooks off (ldo.c, luaD_hook) --
// a handler that loops would then run for ever. Without the debug library a handler has no use for
// the stack at the raise anyway. Returns true plus f's results, or false plus the handler's result.
static int RP_Lua_XPcall( lua_State *L )
{
	int n = lua_gettop( L );

	luaL_checkany( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );
	lua_pushboolean( L, 1 );	// f h args... true f
	lua_pushvalue( L, 1 );
	lua_rotate( L, 3, 2 );		// f h true f args...
	if ( lua_pcall( L, n - 2, LUA_MULTRET, 0 ) == LUA_OK )
		return lua_gettop( L ) - 2;	// true and the results
	lua_pushvalue( L, 2 );		// f h true err h err
	lua_pushvalue( L, -2 );
	lua_call( L, 1, 1 );		// may raise (a killed call does): it climbs further, as it should
	lua_pushboolean( L, 0 );
	lua_pushvalue( L, -2 );
	return 2;
}

static void RP_Lua_OpenSandbox( rpLuaPlugin_t *p )
{
	lua_State *L = p->L;
	static const char *removed[] = { "dofile", "loadfile", "loadstring", "require", NULL };
	int i;

	luaL_requiref( L, LUA_GNAME, luaopen_base, 1 );		lua_pop( L, 1 );
	luaL_requiref( L, LUA_STRLIBNAME, luaopen_string, 1 );	lua_pop( L, 1 );
	luaL_requiref( L, LUA_TABLIBNAME, luaopen_table, 1 );	lua_pop( L, 1 );
	luaL_requiref( L, LUA_MATHLIBNAME, luaopen_math, 1 );	lua_pop( L, 1 );
	luaL_requiref( L, LUA_UTF8LIBNAME, luaopen_utf8, 1 );	lua_pop( L, 1 );

	for ( i = 0; removed[i]; i++ )
	{
		lua_pushnil( L );
		lua_setglobal( L, removed[i] );
	}
	lua_pushcfunction( L, RP_Lua_Print );			lua_setglobal( L, "print" );
	lua_pushcfunction( L, RP_Lua_Load );			lua_setglobal( L, "load" );
	lua_pushcfunction( L, RP_Lua_CollectGarbage );	lua_setglobal( L, "collectgarbage" );
	lua_pushcfunction( L, RP_Lua_SetMetatable );	lua_setglobal( L, "setmetatable" );
	lua_pushcfunction( L, RP_Lua_XPcall );			lua_setglobal( L, "xpcall" );

	// the string metatable is shared by every string: lock it
	lua_pushliteral( L, "" );
	if ( lua_getmetatable( L, -1 ) )
	{
		lua_pushboolean( L, 0 );
		lua_setfield( L, -2, "__metatable" );
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );

	RP_LuaApi_Open( p );
	RP_LuaStore_OpenLib( p );
	RP_LuaWorld_OpenLib( p );
}

/*
==================
Loading
==================
*/
qboolean RP_Lua_ValidName( const char *name )
{
	int i;

	if ( !name || !name[0] || strlen( name ) > RP_LUA_NAME_LEN )
		return qfalse;
	for ( i = 0; name[i]; i++ )
	{
		char c = name[i];

		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) )
			return qfalse;
	}
	return qtrue;
}

rpLuaPlugin_t *RP_Lua_Find( const char *name )
{
	int i;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		if ( rpLuaPlugins[i].inUse && !Q_stricmp( rpLuaPlugins[i].name, name ) )
			return &rpLuaPlugins[i];
	}
	return NULL;
}

// the plugin's file, in the mod folder on disk (never a pk3: a download must not bring code)
static void RP_Lua_PluginPath( const char *name, char *path, int size )
{
	char home[MAX_OSPATH] = { 0 };

	trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
	if ( home[0] )
		Com_sprintf( path, size, "%s/%s/lua/plugins/%s/plugin.lua", home, RP_MOD_FOLDER, name );
	else
		Com_sprintf( path, size, "%s/lua/plugins/%s/plugin.lua", RP_MOD_FOLDER, name );
}

qboolean RP_Lua_PluginFileExists( const char *name )
{
	char path[MAX_OSPATH * 2];
	FILE *f;

	if ( !RP_Lua_ValidName( name ) )
		return qfalse;
	RP_Lua_PluginPath( name, path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( !f )
		return qfalse;
	fclose( f );
	return qtrue;
}

static char *RP_Lua_ReadPlugin( const char *name, size_t *len )
{
	char path[MAX_OSPATH * 2];
	FILE *f;
	long size;
	char *buf;

	RP_Lua_PluginPath( name, path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( !f )
		return NULL;
	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( size <= 0 || size > 256 * 1024 )
	{
		fclose( f );
		*len = (size_t)size;
		return NULL;
	}
	buf = (char *)malloc( (size_t)size );
	if ( !buf || fread( buf, 1, (size_t)size, f ) != (size_t)size )
	{
		free( buf );
		fclose( f );
		return NULL;
	}
	fclose( f );
	*len = (size_t)size;
	return buf;
}

static void RP_Lua_CountActive( void )
{
	int i, n = 0, w = 0;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		if ( rpLuaPlugins[i].inUse && rpLuaPlugins[i].L )
		{
			n++;
			w += rpLuaPlugins[i].numWatches;
		}
	}
	rp_luaActive = n;
	rp_luaWatchCount = w;
}

static void RP_Lua_ResetPlugin( rpLuaPlugin_t *p )
{
	int i;

	memset( p, 0, sizeof( *p ) );
	for ( i = 0; i < RPLE_COUNT; i++ )
		p->handlers[i] = LUA_NOREF;
	p->tokens = RP_LUA_TOKENS;
	p->frameMsLeft = RP_LUA_PLUGIN_FRAME_MS;
	p->flushCursor = -1;
	p->nextTimerId = 1;
}

// the state closed and the slot kept (disabled) or freed
static void RP_Lua_ClosePlugin( rpLuaPlugin_t *p, qboolean keepSlot )
{
	char name[RP_LUA_NAME_LEN + 1];
	int errorsTotal = p->errorsTotal;

	Q_strncpyz( name, p->name, sizeof( name ) );
	if ( p->name[0] )
		RP_Lua_MapSpawnsSave( p );	// the map's counters outlive the state
	RP_LuaWorld_PluginClose( p );	// its props and NPCs go with it (runs at a frame end or map end only)
	if ( p->L && rpLuaPanicked )
		p->L = NULL;	// left as it is: a half-built state is not closed (its memory goes with the process)
	if ( p->L )
	{
		RP_LuaApi_ClosePlugin( p );
		RP_LuaStore_PluginClose( p );
		lua_close( p->L );
		p->L = NULL;
	}
	if ( p->pending )
	{
		int i;

		for ( i = 0; i < p->numPending; i++ )
			free( p->pending[i].value );
		free( p->pending );
		p->pending = NULL;
	}
	RP_Lua_ResetPlugin( p );
	if ( keepSlot )
	{
		Q_strncpyz( p->name, name, sizeof( p->name ) );
		p->inUse = qtrue;
		p->disabled = qtrue;
		p->errorsTotal = errorsTotal;
	}
	RP_Lua_CountActive();
}

static qboolean RP_Lua_LoadPlugin( const char *name, const char **why )
{
	rpLuaPlugin_t *p = RP_Lua_Find( name );
	char *buf;
	size_t len = 0;
	int i;

	*why = NULL;
	if ( !RP_Lua_ValidName( name ) )
	{
		*why = "not a plugin name (letters, digits, '_' and '-', up to 32)";
		return qfalse;
	}
	if ( p && p->L )
	{
		*why = "already loaded";
		return qfalse;
	}
	if ( !p )
	{
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
			*why = va( "no room: %d plugins at most", RP_LUA_MAX_PLUGINS );
			return qfalse;
		}
	}

	buf = RP_Lua_ReadPlugin( name, &len );
	if ( !buf )
	{
		*why = ( len > 256 * 1024 ) ? "plugin.lua is longer than 256 KB" : va( "lua/plugins/%s/plugin.lua not found in the mod folder", name );
		return qfalse;
	}

	RP_Lua_ResetPlugin( p );
	Q_strncpyz( p->name, name, sizeof( p->name ) );
	RP_Lua_MapSpawnsRestore( p );
	p->inUse = qtrue;
	p->memCap = RP_Lua_MemCap();
	p->L = lua_newstate( RP_Lua_Alloc, p );
	if ( !p->L )
	{
		free( buf );
		RP_Lua_ResetPlugin( p );
		*why = "could not create the state";
		return qfalse;
	}
	lua_atpanic( p->L, RP_Lua_Panic );
	RP_Lua_OpenSandbox( p );
	RP_LuaStore_PluginInit( p );
	RP_Lua_CountActive();

	if ( luaL_loadbufferx( p->L, buf, len, va( "=%s", name ), "t" ) != LUA_OK )
	{
		*why = va( "%s", lua_tostring( p->L, -1 ) );
		G_LogPrintf( "[lua:%s] load error: %s\n", name, *why );
		free( buf );
		RP_Lua_ClosePlugin( p, qtrue );
		return qfalse;
	}
	free( buf );

	if ( !RP_Lua_Call( p, 0, 0 ) )
	{
		*why = "the chunk ended in an error (see the log)";
		RP_Lua_ClosePlugin( p, qtrue );
		return qfalse;
	}

	// init
	RP_Lua_CallRef( p, p->handlers[RPLE_INIT], RP_LUA_CALL_MS, RP_Lua_PushEmptyEvent, NULL );
	G_LogPrintf( "[lua:%s] loaded\n", name );
	return qtrue;
}

static int RP_Lua_PushShutdownEvent( lua_State *L, void *ctx )
{
	lua_newtable( L );
	lua_pushstring( L, (const char *)ctx );
	lua_setfield( L, -2, "reason" );
	return 1;
}

static void RP_Lua_FireShutdown( rpLuaPlugin_t *p, const char *reason )
{
	if ( p->L && !rpLuaPanicked )
		RP_Lua_CallRef( p, p->handlers[RPLE_SHUTDOWN], RP_LUA_CALL_MS, RP_Lua_PushShutdownEvent, (void *)reason );
}

// a state for rcon "lua <code>": the sandbox and budget of a plugin, closed by the caller at once
qboolean RP_Lua_OpenScratch( rpLuaPlugin_t *p, const char *name )
{
	RP_Lua_ResetPlugin( p );
	Q_strncpyz( p->name, name, sizeof( p->name ) );
	RP_Lua_MapSpawnsRestore( p );
	p->inUse = qtrue;
	p->memCap = RP_Lua_MemCap();
	p->L = lua_newstate( RP_Lua_Alloc, p );
	if ( !p->L )
	{
		RP_Lua_ResetPlugin( p );
		return qfalse;
	}
	lua_atpanic( p->L, RP_Lua_Panic );
	RP_Lua_OpenSandbox( p );
	RP_LuaStore_PluginInit( p );
	RP_Lua_CountActive();
	return qtrue;
}

void RP_Lua_CloseScratch( rpLuaPlugin_t *p )
{
	RP_Lua_ClosePlugin( p, qfalse );
}

/*
==================
The queue: every close happens here, at the end of the frame
==================
*/
qboolean RP_Lua_QueueOp( const char *name, rpLuaOp_t op )
{
	rpLuaPlugin_t *p = RP_Lua_Find( name );

	if ( !p || !p->L )
		return qfalse;
	p->pendingOp = op;
	return qtrue;
}

qboolean RP_Lua_QueueLoad( const char *name )
{
	int i;

	if ( !RP_Lua_ValidName( name ) || rpLuaNumLoadQueued >= RP_LUA_MAX_PLUGINS )
		return qfalse;
	for ( i = 0; i < rpLuaNumLoadQueued; i++ )
	{
		if ( !Q_stricmp( rpLuaLoadQueue[i], name ) )
			return qtrue;
	}
	Q_strncpyz( rpLuaLoadQueue[rpLuaNumLoadQueued++], name, RP_LUA_NAME_LEN + 1 );
	return qtrue;
}

static void RP_Lua_RunQueue( void )
{
	int i;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];
		rpLuaOp_t op = p->pendingOp;

		if ( !p->inUse || op == RPLOP_NONE )
			continue;
		p->pendingOp = RPLOP_NONE;
		switch ( op )
		{
		case RPLOP_DISABLE:
			RP_Lua_FireShutdown( p, "disabled" );
			RP_Lua_ClosePlugin( p, qtrue );
			break;
		case RPLOP_UNLOAD:
			RP_Lua_FireShutdown( p, "unloaded" );
			RP_Lua_ClosePlugin( p, qfalse );
			break;
		case RPLOP_RELOAD:
			RP_Lua_FireShutdown( p, "reloaded" );
			RP_Lua_ClosePlugin( p, qtrue );
			RP_Lua_QueueLoad( p->name );
			break;
		default:
			break;
		}
	}

	for ( i = 0; i < rpLuaNumLoadQueued; i++ )
	{
		const char *why;

		if ( !RP_Lua_LoadPlugin( rpLuaLoadQueue[i], &why ) )
			G_LogPrintf( "[lua] %s not loaded: %s\n", rpLuaLoadQueue[i], why );
	}
	rpLuaNumLoadQueued = 0;
}

/*
==================
Events
==================
*/
typedef struct {
	void	(*fill)( lua_State *L, void *ctx );
	void	*ctx;
} rpLuaEventReq_t;

// the event table: {} filled by the hook's fill(), inside the protected call
static int RP_Lua_PushEvent( lua_State *L, void *ctx )
{
	rpLuaEventReq_t *e = (rpLuaEventReq_t *)ctx;

	lua_newtable( L );
	if ( e->fill )
		e->fill( L, e->ctx );
	return 1;
}

int RP_Lua_PushEmptyEvent( lua_State *L, void *ctx )
{
	(void)ctx;
	lua_newtable( L );
	return 1;
}

// an event call past twice the plugin's frame budget is not made (RP_Lua_CallMs: past the budget it runs with
// the short deadline); init and shutdown always are. Logged once a minute per plugin.
qboolean RP_Lua_EventRoom( rpLuaPlugin_t *p, rpLuaEvent_t ev )
{
	if ( ev == RPLE_INIT || ev == RPLE_SHUTDOWN || p->frameMsLeft > -RP_LUA_PLUGIN_FRAME_MS )
		return qtrue;
	p->eventsSkipped++;
	if ( level.time - p->lastSkipLog >= 60000 || !p->lastSkipLog )
	{
		p->lastSkipLog = level.time;
		G_LogPrintf( "[lua:%s] %s event not run: the plugin's frame budget (%d ms) is spent twice over\n", p->name, rpLuaEventNames[ev], RP_LUA_PLUGIN_FRAME_MS );
	}
	return qfalse;
}

// one plugin's handler for an event, pushed and run inside the protected call
void RP_Lua_FirePlugin( rpLuaPlugin_t *p, rpLuaEvent_t ev, void (*fill)( lua_State *L, void *ctx ), void *ctx )
{
	rpLuaEventReq_t e;

	if ( !p->inUse || !p->L || p->handlers[ev] == LUA_NOREF || !RP_Lua_EventRoom( p, ev ) )
		return;
	e.fill = fill;
	e.ctx = ctx;
	RP_Lua_CallRef( p, p->handlers[ev], ( ev == RPLE_INIT || ev == RPLE_SHUTDOWN ) ? RP_LUA_CALL_MS : RP_Lua_CallMs( p ), RP_Lua_PushEvent, &e );
}

void RP_Lua_FireEvent( rpLuaEvent_t ev, void (*fill)( lua_State *L, void *ctx ), void *ctx )
{
	int i;

	if ( rpLuaCurrent )
	{ // raised by what a script call did (a World.use that paid a level up, replaced a dialogue, killed a player)
		G_LogPrintf( "[lua:%s] %s event raised inside a script call: not delivered\n", rpLuaCurrent->name, rpLuaEventNames[ev] );
		return;
	}
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
		RP_Lua_FirePlugin( &rpLuaPlugins[i], ev, fill, ctx );
}

void RP_Lua_RefillTokens( rpLuaPlugin_t *p )
{
	int flood = trap->Cvar_VariableIntegerValue( "sv_floodProtect" );
	int ms = ( flood <= 1 ) ? 1000 : flood;

	if ( p->tokenRefill == 0 )
		p->tokenRefill = level.time + ms;
	while ( level.time >= p->tokenRefill )
	{
		if ( p->tokens < RP_LUA_TOKENS )
			p->tokens++;
		p->tokenRefill += ms;
	}
}

/*
==================
Lifecycle
==================
*/
void RP_Lua_Init( void )
{
	char list[MAX_CVAR_VALUE_STRING], *s, *tok;
	int i;

	memset( rpLuaPlugins, 0, sizeof( rpLuaPlugins ) );
	memset( rpLuaGen, 0, sizeof( rpLuaGen ) );
	rpLuaNumLoadQueued = 0;
	rp_luaActive = 0;
	rp_luaWatchCount = 0;
	rp_luaInCall = 0;
	rpLuaCurrent = NULL;
	rpLuaPanicked = qfalse;
	rpLuaNumMapNames = 0;	// a new map: the per-name spawn counters start over
	RP_LuaApi_Reset();
	RP_LuaWorld_Reset();

	rpLuaStoreReady = RP_LuaStore_Open();
	if ( rpLuaStoreReady )
		RP_LuaStore_SweepOrphans();

	Q_strncpyz( list, rp_lua_plugins.string, sizeof( list ) );
	for ( s = list; ( tok = strtok( s, " ,;" ) ) != NULL; s = NULL )
	{
		const char *why;

		if ( !RP_Lua_LoadPlugin( tok, &why ) )
			G_LogPrintf( "[lua] %s not loaded: %s\n", tok, why );
	}
	for ( i = 0; i < MAX_CLIENTS; i++ )
		rpLuaGen[i] = 1;
}

void RP_Lua_Shutdown( void )
{
	int i;

	rpLuaCurrent = NULL;	// an ERR_DROP inside a script call left them set
	rp_luaInCall = 0;
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse )
			continue;
		RP_Lua_FireShutdown( p, "map_end" );
		RP_Lua_ClosePlugin( p, qfalse );
	}
	RP_LuaStore_Close();
	rpLuaStoreReady = qfalse;
}

void RP_Lua_Frame( void )
{
	int i;

	rpLuaFrameMsLeft = RP_LUA_FRAME_MS;
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		rpLuaPlugin_t *p = &rpLuaPlugins[i];

		if ( !p->inUse || !p->L )
			continue;
		p->frameMsLeft = RP_LUA_PLUGIN_FRAME_MS;
		RP_Lua_RefillTokens( p );
		RP_LuaApi_RunTimers( p );
		RP_LuaStore_Frame( p );
	}
	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{
		if ( rpLuaPlugins[i].inUse && rpLuaPlugins[i].removesQueued )
			RP_LuaWorld_Frame( &rpLuaPlugins[i] );	// World.remove, after every call of the frame
	}
	RP_Lua_RunQueue();
	RP_LuaWorld_ReleasedFrame();	// ridden vehicles let go of at a close, removed once empty
}

void RP_Lua_Generation( int clientNum )
{
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS )
		rpLuaGen[clientNum]++;
}

void RP_Lua_CharacterRemoved( int charId )
{
	if ( rpLuaStoreReady )
		RP_LuaStore_CharacterRemoved( charId );
}
