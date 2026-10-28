// TEST ONLY: the worst shapes from the first attempt's audits, P1 reach only. Each must end within
// RP_LUA_LIMIT_MS of real time, drop nobody (an emulated engine queue drops a client 129 commands behind),
// and leave the state closable.
#include "t.h"

#define RP_LUA_LIMIT_MS 60

typedef struct { const char *name; const char *code; } shape_t;
static const shape_t shapes[] = {
	{ "strbomb",	"local s = 'x' for i = 1, 40 do s = s .. s end" },
	{ "patbomb",	"local s = ('a'):rep(200000) s:find('.-.-.-.-x')" },
	{ "patclass",	"local s = ('a'):rep(200000) s:find('[' .. ('a'):rep(1000) .. ']+b')" },
	{ "gsub",		"local s = ('a'):rep(200000) s:gsub('a', function(c) return c .. c end)" },
	{ "format",		"string.format('%q', ('\\1'):rep(200000))" },
	{ "nextempty",	"local t = {} for i = 1, 300000 do t[i] = i end for i = 1, 300000 do t[i] = nil end for k in pairs(t) do end for i = 1, 200000 do next(t) end" },
	{ "tablesort",	"local t = {} for i = 1, 200000 do t[i] = -i end table.sort(t, function(a, b) return a < b end)" },
	{ "varargs",	"local t = {} for i = 1, 400000 do t[i] = i end local f = function(...) return select('#', ...) end f(table.unpack(t))" },
	{ "concat",		"local t = {} for i = 1, 300000 do t[i] = 'abc' end local s = table.concat(t) for i = 1, 50 do s = s .. s end" },
	{ "recurse",	"local function f(n) return f(n + 1) + 1 end f(1)" },
	{ "ccalls",		"local t = setmetatable({}, {__index = function(t, k) return t[k] end}) local x = t.foo" },
	{ "memloop",	"local t = {} while true do t[#t + 1] = ('x'):rep(1000) end" },
	{ "broadcast",	"for i = 1, 100000 do Msg.broadcast('spam ' .. i) end" },
	{ "printloop",	"local ps = Players.all() for i = 1, 100000 do for _, p in ipairs(ps) do Msg.print(p, 'x') Msg.center(p, 'y') end end" },
	{ "timerflood",	"for i = 1, 100000 do Timer.every(50, function() Msg.broadcast('t') end) end" },
	{ "watchflood",	"for i = 1, 100000 do World.watch('w' .. i) end" },
	{ "cmdflood",	"for i = 1, 100000 do Commands.add('c' .. i, function() end) end" },
	{ "storechurn",	"for i = 1, 100000 do Storage.set('k' .. (i % 500), ('v'):rep(2000)) Storage.del('k' .. (i % 500)) end" },
	{ "logflood",	"for i = 1, 100000 do Log.info('x') print('y') end" },
	{ "errorloop",	"for i = 1, 100 do pcall(error, 'e') end error('final')" },
	{ "within",		"for i = 1, 100000 do World.players_within({x=0,y=0,z=0}, 8192) end" },
	{ "entityfind",	"for i = 1, 100000 do World.entity('door') end" },
	{ "cvar",		"for i = 1, 100000 do Cvar.get('g_gametype') end" },
	{ "loadloop",	"for i = 1, 100000 do load('return ' .. i)() end" },
	{ "shutdownerr","Events.on('shutdown', function() error('in shutdown') end)" },
	{ "deep",		"local t = {} local c = t for i = 1, 100000 do c.n = {} c = c.n end" },
	{ "utf8",		"local s = ('\\xF0\\x9F\\x98\\x80'):rep(50000) for i = 1, 100 do utf8.len(s) end" },
	{ "tostringloop","local t = {} for i = 1, 100000 do t[i] = tostring(i) .. tostring({}) end" },
	{ NULL, NULL }
};

int main( void )
{
	int i, c;
	t_fresh(); t_init( 24 );
	for ( c = 0; c < 24; c++ ) t_player( c, va( "P%d", c ), qtrue, 100 + c );
	for ( i = 0; shapes[i].name; i++ )
	{
		rpLuaPlugin_t *p;
		int t0, took, k, maxq = 0;
		char code[4096];

		Com_sprintf( code, sizeof( code ), "Events.on('init', function() %s end)\n", shapes[i].code );
		t_write_plugin( "h", code );
		t_logLen = 0; t_log[0] = 0; t_clear_sent();
		t0 = trap->Milliseconds();
		t_start( "h" );
		took = trap->Milliseconds() - t0;
		p = RP_Lua_Find( "h" );
		for ( k = 0; k < 24; k++ ) if ( t_nsent[k] > maxq ) maxq = t_nsent[k];
		CHECK( took < RP_LUA_LIMIT_MS, "%s: init took %d ms", shapes[i].name, took );
		CHECK( maxq < 8, "%s: %d commands to one player", shapes[i].name, maxq );
		CHECK( p && p->inUse, "%s: plugin slot", shapes[i].name );
		if ( p ) CHECK( p->memUsed <= 512 * 1024, "%s: memory %d", shapes[i].name, (int)p->memUsed );
		t0 = trap->Milliseconds();
		for ( k = 0; k < 20; k++ ) t_frame( 50 );	// timers and flushes
		took = trap->Milliseconds() - t0;
		CHECK( took < 20 * RP_LUA_LIMIT_MS, "%s: 20 frames took %d ms", shapes[i].name, took );
		for ( k = 0, maxq = 0; k < 24; k++ ) if ( t_nsent[k] > maxq ) maxq = t_nsent[k];
		CHECK( maxq < 129, "%s: %d commands queued to one player over a second", shapes[i].name, maxq );
		t0 = trap->Milliseconds();
		RP_LUA_SHUTDOWN();
		took = trap->Milliseconds() - t0;
		CHECK( took < RP_LUA_LIMIT_MS * 2, "%s: shutdown took %d ms", shapes[i].name, took );
		CHECK( rp_luaActive == 0 && rp_luaWatchCount == 0, "%s: clean after shutdown (%d %d)", shapes[i].name, rp_luaActive, rp_luaWatchCount );
		printf( "%-14s ok\n", shapes[i].name );
	}
	printf( fails ? "%d FAILED\n" : "t_hostile: ALL OK\n", fails );
	return fails != 0;
}
