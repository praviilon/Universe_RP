// TEST ONLY: the state, the sandbox, the budget, the errors, the queue
#include "t.h"

int main( void )
{
	rpLuaPlugin_t *p;
	t_fresh(); t_init( 8 );

	// --- removed functions and the sandbox
	t_write_plugin( "sb", "R = {} for _, n in ipairs{'dofile','loadfile','loadstring','require','io','os','package','debug','coroutine'} do R[n] = _G[n] == nil end\n"
		"R.load_text = (load('return 1') ~= nil) R.load_bin = (load(string.dump(function() end)) == nil) R.gc = pcall(collectgarbage, 'collect') == false R.gccount = type(collectgarbage('count')) == 'number'\n"
		"R.strmeta = (getmetatable('') == false) R.api = PLUGIN_NAME == 'sb' and API_VERSION == 1 and type(Events.on) == 'function' and type(Storage.char.get) == 'function'\n"
		"print('hello', 1)\n" );
	t_start( "sb" );
	p = RP_Lua_Find( "sb" ); CHECK( p && p->L, "sb loaded" );
	if ( p && p->L )
	{
		const char *keys[] = { "dofile","loadfile","loadstring","require","io","os","package","debug","coroutine","load_text","load_bin","gc","gccount","strmeta","api", NULL };
		int i;
		lua_getglobal( p->L, "R" );
		for ( i = 0; keys[i]; i++ ) { lua_getfield( p->L, -1, keys[i] ); CHECK( lua_toboolean( p->L, -1 ), "sandbox %s", keys[i] ); lua_pop( p->L, 1 ); }
		lua_pop( p->L, 1 );
	}
	CHECK( t_log_has( "[lua:sb] hello\t1" ), "print to the log" );
	CHECK( rp_luaActive == 1, "active %d", rp_luaActive );
	RP_LUA_SHUTDOWN(); CHECK( rp_luaActive == 0, "shutdown" );

	// --- a bytecode file is refused
	t_init( 8 ); { FILE *f; t_mkdirs( "./home/" RP_MOD_FOLDER "/lua/plugins/bin" ); f = fopen( "./home/" RP_MOD_FOLDER "/lua/plugins/bin/plugin.lua", "wb" ); fputs( "\x1bLua\x54\x00", f ); fclose( f ); }
	t_start( "bin" ); p = RP_Lua_Find( "bin" ); CHECK( !p || !p->L, "bytecode refused" ); CHECK( t_log_has( "not loaded" ), "refusal logged" ); RP_LUA_SHUTDOWN();

	// --- the time budget: a loop stops at about RP_LUA_CALL_MS on the real clock
	t_init( 8 ); t_write_plugin( "loop", "Events.on('init', function() while true do end end)\n" );
	{ int t0 = trap->Milliseconds(); t_start( "loop" ); CHECK( trap->Milliseconds() - t0 < 100, "loop stopped in %d ms", trap->Milliseconds() - t0 ); }
	CHECK( t_log_has( "ran too long" ), "ran too long logged" );
	p = RP_Lua_Find( "loop" ); CHECK( p && p->L && p->errorsTotal == 1, "one error, still running" ); RP_LUA_SHUTDOWN();

	// --- the instruction cap with the clock frozen
	t_init( 8 ); t_clockFrozen = qtrue; t_write_plugin( "ins", "Events.on('init', function() local i = 0 while true do i = i + 1 end end)\n" );
	t_start( "ins" ); CHECK( t_log_has( "instructions" ), "instruction cap: %s", strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" ); t_clockFrozen = qfalse; RP_LUA_SHUTDOWN();

	// --- memory: a big string is refused and the state still works
	t_init( 8 ); t_write_plugin( "mem", "ok, err = pcall(string.rep, 'x', 600000) after = (('y'):rep(10)) R = { ok = ok, err = err, after = after }\n" );
	t_start( "mem" ); p = RP_Lua_Find( "mem" ); CHECK( p && p->L, "mem loaded" );
	if ( p && p->L ) { lua_getglobal( p->L, "R" ); lua_getfield( p->L, -1, "ok" ); CHECK( !lua_toboolean( p->L, -1 ), "rep refused" ); lua_pop( p->L, 1 ); lua_getfield( p->L, -1, "err" ); CHECK( strstr( lua_tostring( p->L, -1 ), "memory" ) != NULL, "memory error: %s", lua_tostring( p->L, -1 ) ); lua_pop( p->L, 1 ); lua_getfield( p->L, -1, "after" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "yyyyyyyyyy" ), "state works after" ); lua_pop( p->L, 2 ); CHECK( p->memPeak < 600 * 1024, "peak %d", (int)p->memPeak ); }
	RP_LUA_SHUTDOWN();

	// --- the stack limit: {...} and table.pack of 30000 arguments stop
	t_init( 8 ); t_write_plugin( "stk", "local t = {} for i = 1, 30000 do t[i] = i end R = { a = pcall(function() return select('#', table.unpack(t)) end), b = pcall(function() return #{table.unpack(t)} end) }\n" );
	t_start( "stk" ); p = RP_Lua_Find( "stk" ); if ( p && p->L ) { lua_getglobal( p->L, "R" ); lua_getfield( p->L, -1, "a" ); CHECK( !lua_toboolean( p->L, -1 ), "unpack 30000 refused" ); lua_pop( p->L, 2 ); } RP_LUA_SHUTDOWN();

	// --- errors: 20 in a minute switch the plugin off at the frame end, not before
	t_init( 8 ); t_write_plugin( "err", "Events.on('frame', function() error('boom') end)\n" );
	t_start( "err" ); p = RP_Lua_Find( "err" );
	{ int i; for ( i = 0; i < 19; i++ ) t_frame( 50 ); CHECK( p->L && p->errorsTotal == 19, "19 errors, still on (%d)", p->errorsTotal );
	  t_frame( 50 ); CHECK( !p->L && p->disabled && p->inUse, "20th error: off at the frame end (L %p)", (void *)p->L ); CHECK( t_log_has( "switched off" ), "switched off logged" ); }
	// enable loads again with a fresh count
	RP_Lua_QueueLoad( "err" ); t_frame( 50 ); CHECK( p->L && p->errorsTotal == 0, "enabled again: %d errors", p->errorsTotal ); RP_LUA_SHUTDOWN();

	// --- the frame budget: slow timers past the plugin's 20 ms are deferred, never dropped, never an error
	// (a frozen clock that moves 1 ms on every read: each timer runs about 500 instructions, five hook reads, so about 5 ms whatever the build)
	t_init( 8 ); t_clockFrozen = qtrue; t_tick = 1; t_write_plugin( "slow", "N = 0 for i = 1, 8 do Timer.every(50, function() N = N + 1 local x = 0 for k = 1, 160 do x = x + k end end) end\n" );
	t_start( "slow" ); p = RP_Lua_Find( "slow" );
	t_frame( 60 ); { int n; lua_getglobal( p->L, "N" ); n = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); CHECK( n >= 1 && n < 8 && p->deferredTimers >= 1, "deferred: ran %d of 8, deferred %d, errors %d", n, p->deferredTimers, p->errorsTotal ); CHECK( p->errorsTotal == 0, "no error for a slow frame: %d: %.300s", p->errorsTotal, strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" ); if ( !p->L ) return 1; }
	{ int i, n; for ( i = 0; i < 10; i++ ) t_frame( 50 ); lua_getglobal( p->L, "N" ); n = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); CHECK( n >= 8, "all eight ran eventually: %d", n ); }
	RP_LUA_SHUTDOWN(); t_clockFrozen = qfalse; t_tick = 0;

	// --- the queue: a reload asked from inside a handler happens at the frame end, and shutdown runs
	t_init( 8 ); t_write_plugin( "q", "Events.on('shutdown', function(e) Log.info('bye ' .. e.reason) end)\n" );
	t_start( "q" ); p = RP_Lua_Find( "q" ); { lua_State *L0 = p->L; lua_pushboolean( L0, 1 ); lua_setglobal( L0, "OLD" ); RP_Lua_QueueOp( "q", RPLOP_RELOAD ); CHECK( p->L == L0, "not closed yet" ); t_frame( 50 ); lua_getglobal( p->L, "OLD" ); CHECK( p->L && lua_isnil( p->L, -1 ), "reloaded at the frame end (a fresh state)" ); lua_pop( p->L, 1 ); CHECK( t_log_has( "bye reloaded" ), "shutdown event on reload" ); }
	RP_Lua_QueueOp( "q", RPLOP_UNLOAD ); t_frame( 50 ); CHECK( !p->inUse, "unloaded: slot free" ); CHECK( t_log_has( "bye unloaded" ), "shutdown on unload" ); RP_LUA_SHUTDOWN();

	// --- rp_lua 0: nothing happens
	t_init( 8 ); rp_lua.integer = 0; t_start( "q" ); CHECK( rp_luaActive == 0 && !RP_Lua_Find( "q" ), "rp_lua 0 loads nothing" ); RP_LUA_SHUTDOWN(); rp_lua.integer = 1;

	// --- rcon lua
	t_init( 8 ); t_start( "" ); t_args( "lua return 1 + 1, 'x'" ); Svcmd_Lua_f(); CHECK( strstr( t_console, "2\tx" ) != NULL, "rcon lua: %s", t_console ); CHECK( rp_luaActive == 0, "scratch state closed" );
	t_args( "lua error('e')" ); Svcmd_Lua_f(); CHECK( t_log_has( "[lua:rcon] error" ), "rcon error logged" ); RP_LUA_SHUTDOWN();

	printf( fails ? "%d FAILED\n" : "t_core: ALL OK\n", fails );
	return fails != 0;
}
