// TEST ONLY: the state, the sandbox, the budget, the errors, the queue
#include "t.h"

int main( void )
{
	rpLuaPlugin_t *p;
	t_fresh(); t_init( 8 );

	// --- removed functions and the sandbox
	t_write_plugin( "sb", "R = {} for _, n in ipairs{'dofile','loadfile','loadstring','require','io','os','package','debug','coroutine'} do R[n] = _G[n] == nil end\n"
		"R.load_text = (load('return 1') ~= nil) R.load_bin = (load(string.dump(function() end)) == nil) R.gc = pcall(collectgarbage, 'collect') == false R.gccount = type(collectgarbage('count')) == 'number'\n"
		"R.strmeta = (getmetatable('') == false) R.api = PLUGIN_NAME == 'sb' and API_VERSION == 2 and type(Events.on) == 'function' and type(Storage.char.get) == 'function' and type(World.use) == 'function' and type(Effect.play) == 'function'\n"
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

	// --- AUDIT: a call while another is on the stack is refused, with the stack left as it was
	t_init( 8 ); t_write_plugin( "n1", "N = 0 function F() N = N + 1 end\n" ); t_write_plugin( "n2", "\n" ); t_start( "n1 n2" );
	{ rpLuaPlugin_t *a = RP_Lua_Find( "n1" ), *b = RP_Lua_Find( "n2" ); int top;
	  CHECK( a && a->L && b && b->L, "nested: both loaded" );
	  if ( a && a->L && b && b->L ) { rpLuaCurrent = b; top = lua_gettop( a->L ); lua_getglobal( a->L, "F" );
	    CHECK( !RP_Lua_Call( a, 0, 0 ) && lua_gettop( a->L ) == top, "nested call refused, stack balanced" ); rpLuaCurrent = NULL;
	    lua_getglobal( a->L, "N" ); CHECK( lua_tointeger( a->L, -1 ) == 0, "the refused function did not run" ); lua_pop( a->L, 1 );
	    CHECK( t_log_has( "nested script call refused" ), "nested refusal logged" ); } }
	RP_LUA_SHUTDOWN();

	// --- AUDIT C1: a plugin at its memory cap, then events, frames and a shutdown: errors at worst, never an abort
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "full", "Keep = {} N = 0 Events.on('frame', function() N = N + 1 end) Events.on('chat', function(e) N = N + 1 end) Events.on('shutdown', function() end)\n"
		"Events.on('init', function() pcall(function() local i = 0 while true do i = i + 1 Keep[i] = {i, i, i} end end) end)\n" );
	t_start( "full" ); p = RP_Lua_Find( "full" ); CHECK( p && p->L && p->memUsed > p->memCap - 32 * 1024, "filled to the cap: %d of %d", p ? (int)p->memUsed : -1, p ? (int)p->memCap : -1 );
	{ int i; for ( i = 0; i < 5; i++ ) { t_frame( 50 ); RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_ALL, "a line of chat that needs a few hundred bytes of fresh memory for its event table and its strings, more than the plugin has left" ); } }
	CHECK( p->inUse, "still here after events at the cap (%d errors, %s)", p->errorsTotal, p->errorsTotal ? "memory errors logged" : "none needed" );
	if ( p->errorsTotal ) CHECK( t_log_has( "not enough memory" ), "the error is the memory one" );
	RP_LUA_SHUTDOWN(); CHECK( rp_luaActive == 0, "closed with a shutdown handler at the cap" );

	// --- AUDIT C1: rp_lua_memkb applies at the next load, never to a running state
	t_init( 8 ); t_clockFrozen = qtrue; t_write_plugin( "mk", "Keep = {} for i = 1, 1000 do Keep[i] = ('x'):rep(200) .. i end Events.on('frame', function() N = (N or 0) + 1 end)\n" );
	t_start( "mk" ); p = RP_Lua_Find( "mk" ); CHECK( p && p->L && p->memCap == 512 * 1024 && p->memUsed > 200 * 1024, "loaded under 512 KB, using %d", p ? (int)p->memUsed : -1 );
	rp_lua_memkb.integer = 128; t_frame( 50 ); t_frame( 50 ); CHECK( p->L && p->errorsTotal == 0 && p->memCap == 512 * 1024, "lowered live: the running plugin keeps its cap (%d errors)", p->errorsTotal );
	RP_Lua_QueueOp( "mk", RPLOP_RELOAD ); t_frame( 50 ); p = RP_Lua_Find( "mk" ); CHECK( p && !p->L && t_log_has( "not enough memory" ), "reloaded under 128 KB: the chunk's 200 KB fail with a memory error (%s)", p && p->L ? "loaded" : "not loaded" );
	rp_lua_memkb.integer = 512; t_clockFrozen = qfalse; RP_LUA_SHUTDOWN();

	// --- AUDIT C2: __gc is refused at setmetatable; plain metatables and __metatable protection work as before
	t_init( 8 ); t_write_plugin( "gc", "R = {} R.gc = select(2, pcall(setmetatable, {}, {__gc = function() end})) local t = setmetatable({}, {__index = function() return 7 end}) R.idx = t.anything\n"
		"local pr = setmetatable({}, {__metatable = 'locked'}) R.prot = select(2, pcall(setmetatable, pr, {})) R.gm = getmetatable(pr) R.nilmt = (setmetatable({}, nil) ~= nil) R.badarg = select(2, pcall(setmetatable, {}, 5))\n" );
	t_start( "gc" ); p = RP_Lua_Find( "gc" ); CHECK( p && p->L && p->errorsTotal == 0, "gc plugin loaded" );
	if ( p && p->L ) { lua_getglobal( p->L, "R" );
	  lua_getfield( p->L, -1, "gc" ); CHECK( lua_tostring( p->L, -1 ) && strstr( lua_tostring( p->L, -1 ), "__gc is not allowed" ), "__gc refused: %s", lua_tostring( p->L, -1 ) ); lua_pop( p->L, 1 );
	  lua_getfield( p->L, -1, "idx" ); CHECK( lua_tointeger( p->L, -1 ) == 7, "__index works" ); lua_pop( p->L, 1 );
	  lua_getfield( p->L, -1, "prot" ); CHECK( lua_tostring( p->L, -1 ) && strstr( lua_tostring( p->L, -1 ), "protected metatable" ), "__metatable protection kept" ); lua_pop( p->L, 1 );
	  lua_getfield( p->L, -1, "gm" ); CHECK( lua_tostring( p->L, -1 ) && !strcmp( lua_tostring( p->L, -1 ), "locked" ), "getmetatable honours __metatable" ); lua_pop( p->L, 1 );
	  lua_getfield( p->L, -1, "nilmt" ); CHECK( lua_toboolean( p->L, -1 ), "setmetatable(t, nil) returns t" ); lua_pop( p->L, 1 );
	  lua_getfield( p->L, -1, "badarg" ); CHECK( lua_tostring( p->L, -1 ) && strstr( lua_tostring( p->L, -1 ), "nil or table" ), "a bad metatable argument" ); lua_pop( p->L, 2 ); }
	RP_LUA_SHUTDOWN();

	// --- AUDIT C3: a pcall in the script cannot keep a call past its budget
	t_init( 8 ); t_write_plugin( "pc", "Events.on('init', function() while true do pcall(function() while true do end end) end end)\n" );
	{ int t0 = trap->Milliseconds(); t_start( "pc" ); CHECK( trap->Milliseconds() - t0 < 100, "pcall loop stopped in %d ms", trap->Milliseconds() - t0 ); }
	p = RP_Lua_Find( "pc" ); CHECK( p && p->L && p->errorsTotal == 1 && t_log_has( "ran too long" ), "one error counted, plugin on (%d)", p ? p->errorsTotal : -1 ); RP_LUA_SHUTDOWN();
	t_init( 8 ); t_write_plugin( "pc3", "Events.on('init', function() while true do pcall(function() while true do pcall(function() while true do xpcall(function() while true do end end, function(m) while true do end end) end end) end end) end end)\n" );
	{ int t0 = trap->Milliseconds(); t_start( "pc3" ); CHECK( trap->Milliseconds() - t0 < 100, "nested pcall/xpcall loops stopped in %d ms", trap->Milliseconds() - t0 ); }
	p = RP_Lua_Find( "pc3" ); CHECK( p && p->L && p->errorsTotal == 1, "one error (%d)", p ? p->errorsTotal : -1 ); RP_LUA_SHUTDOWN();
	// xpcall still works as a script expects it to: results on success, the handler's result on an error, extra arguments passed
	t_init( 8 ); t_write_plugin( "xp", "R = {} R.a, R.b, R.c = xpcall(function(x, y) return x + y, 'two' end, function(m) return 'h:' .. m end, 2, 3)\n"
		"R.d, R.e = xpcall(function() error('boom', 0) end, function(m) return 'h:' .. m end) R.f = select(2, pcall(xpcall, function() end, 5))\n" );
	t_start( "xp" ); p = RP_Lua_Find( "xp" ); CHECK( p && p->L && p->errorsTotal == 0, "xp loaded" );
	if ( p && p->L ) { lua_getglobal( p->L, "R" );
	  lua_getfield( p->L, -1, "a" ); lua_getfield( p->L, -2, "b" ); lua_getfield( p->L, -3, "c" ); CHECK( lua_toboolean( p->L, -3 ) && lua_tointeger( p->L, -2 ) == 5 && !strcmp( lua_tostring( p->L, -1 ), "two" ), "xpcall success: true, 5, two" ); lua_pop( p->L, 3 );
	  lua_getfield( p->L, -1, "d" ); lua_getfield( p->L, -2, "e" ); CHECK( !lua_toboolean( p->L, -2 ) && lua_tostring( p->L, -1 ) && !strcmp( lua_tostring( p->L, -1 ), "h:boom" ), "xpcall error: false, the handler's value (%s)", lua_tostring( p->L, -1 ) ); lua_pop( p->L, 2 );
	  lua_getfield( p->L, -1, "f" ); CHECK( lua_tostring( p->L, -1 ) && strstr( lua_tostring( p->L, -1 ), "function expected" ), "xpcall checks the handler" ); lua_pop( p->L, 2 ); }
	RP_LUA_SHUTDOWN();
	// the instruction cap with the clock frozen cannot be caught either
	t_init( 8 ); t_clockFrozen = qtrue; t_write_plugin( "pci", "Events.on('init', function() while true do pcall(function() local i = 0 while true do i = i + 1 end end) end end)\n" );
	t_start( "pci" ); p = RP_Lua_Find( "pci" ); CHECK( p && p->L && p->errorsTotal == 1 && t_log_has( "instructions" ), "instruction cap through pcall: %d errors", p ? p->errorsTotal : -1 ); t_clockFrozen = qfalse; RP_LUA_SHUTDOWN();

	// --- AUDIT M8: events past the frame budget run with the 1 ms deadline; past twice the budget they are not run (shutdown is)
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "bud", "N = 0 Events.on('chat', function(e) N = N + 1 if e.text == 'slow' then while true do end end end) Events.on('shutdown', function() Log.info('bye') end)\n" );
	t_start( "bud" ); p = RP_Lua_Find( "bud" );
	RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_ALL, "quick" ); CHECK( p->eventsShort == 0 && p->errorsTotal == 0, "within the budget: the full deadline" );
	p->frameMsLeft = 0; { int t0 = trap->Milliseconds(); RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_ALL, "slow" ); CHECK( trap->Milliseconds() - t0 < 8 && p->eventsShort == 1 && p->errorsTotal == 1 && t_log_has( "ran too long (1 ms)" ), "budget spent: a 1 ms deadline (%d ms, %d short)", trap->Milliseconds() - t0, p->eventsShort ); }
	p->frameMsLeft = -RP_LUA_PLUGIN_FRAME_MS; RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_ALL, "quick" );
	{ int n; lua_getglobal( p->L, "N" ); n = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); CHECK( n == 2 && p->eventsSkipped == 1 && t_log_has( "event not run" ), "twice over: not run (N %d, skipped %d)", n, p->eventsSkipped ); }
	t_frame( 50 ); RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_ALL, "quick" ); { int n; lua_getglobal( p->L, "N" ); n = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); CHECK( n == 3, "the next frame runs them again (N %d)", n ); }
	p->frameMsLeft = -RP_LUA_PLUGIN_FRAME_MS; rpLuaFrameMsLeft = -1; RP_LUA_SHUTDOWN(); CHECK( t_log_has( "[lua:bud] bye" ), "shutdown runs whatever the budget" );

	printf( fails ? "%d FAILED\n" : "t_core: ALL OK\n", fails );
	return fails != 0;
}
