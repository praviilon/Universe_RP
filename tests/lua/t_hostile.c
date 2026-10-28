// TEST ONLY: the worst shapes from the first attempt's audits, and what P2 reaches (rewards, chains of prints). Each must end within
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
	// P2: the world
	{ "proploop",	"for i = 1, 100000 do World.prop('misc_model_breakable', {x=0,y=0,z=0}) end" },
	{ "npcloop",	"for i = 1, 100000 do World.npc('jedi', {x=0,y=0,z=0}) end" },
	{ "spawnrm",	"for i = 1, 100000 do local h = World.prop('misc_model_breakable', {x=0,y=0,z=0}) if h then World.remove(h) end end" },
	{ "useloop",	"local p = Players.get(0) for i = 1, 100000 do World.use('btn1', p) end" },
	{ "usechain",	"local p = Players.get(0) for i = 1, 100 do World.use('loop1', p) end" },
	{ "effectloop",	"for i = 1, 100000 do Effect.play('env/fire', {x=0,y=0,z=0}) Sound.play('sound/a.wav', {x=0,y=0,z=0}) end" },
	{ "rmhandler",	"local h = World.prop('misc_model_breakable', {x=0,y=0,z=0}) Events.on('client_disconnect', function() World.remove(h) World.remove(h) end) Timer.after(50, function() World.remove(h) end)" },
	{ "selfwatch",	"local p = Players.get(0) local h = World.prop('misc_model_breakable', {x=0,y=0,z=0}, nil, nil, {name='me'}) World.watch('lua.h.me') Events.on('entity_used', function() World.use('lua.h.me', p) end) World.use('lua.h.me', p)" },
	{ "keysbomb",	"local t = {} for i = 1, 1000 do t['k' .. i] = ('v'):rep(255) end pcall(World.prop, 'misc_model_breakable', {x=0,y=0,z=0}, nil, t)" },
	{ "bigvalue",	"pcall(World.prop, 'misc_model_breakable', {x=0,y=0,z=0}, nil, {model = ('x'):rep(100000)})" },
	{ "rewardloop",	"local p = Players.get(0) for i = 1, 100000 do World.use('chest_btn', p) end" },
	{ "nanorigin",	"pcall(World.prop, 'misc_model_breakable', {x=0/0, y=1/0, z=-1/0})" },
	{ "vehloop",	"for i = 1, 100000 do World.vehicle('swoop', {x=0,y=0,z=0}) end" },
	{ "vehtypes",	"for i = 1, 100000 do World.vehicle(({'swoop','jedi','novehfile','nosuch','atst'})[i % 5 + 1], {x=0,y=0,z=0}) end" },
	{ "vehspawnrm",	"for i = 1, 100000 do local h = World.vehicle('tauntaun', {x=0,y=0,z=0}) if h then World.remove(h) World.remove(h) end end" },
	{ "vehnpc",		"for i = 1, 100000 do World.npc('xwing', {x=0,y=0,z=0}) end" },
	{ "npcreward",	"for i = 1, 100000 do World.npc('jedi', {x=0,y=0,z=0}, nil, {credits=100000, xp=100}) end" },
	{ "pay200",		"local p = Players.get(0) for i = 1, 100000 do World.use('pay200', p) end" },
	{ "print200",	"local p = Players.get(0) for i = 1, 100000 do World.use('print200', p) end" },
	{ "bcastloop",	"for i = 0, 23 do World.use('bcast1', Players.get(i)) end" },
	{ "timerpay",	"Timer.every(50, function() for i = 0, 23 do World.use('pay200', Players.get(i)) World.use('print200', Players.get(i)) end end)" },
	{ NULL, NULL }
};

// a stand-in target_print: one line to its activator, or to everyone (bcast*)
static void s_HPrint( gentity_t *self, gentity_t *other, gentity_t *activator )
{
	(void)other;
	trap->SendServerCommand( !Q_stricmpn( self->targetname, "bcast", 5 ) ? -1 : activator->s.number, "print \"a line\n\"" );
	if ( self->target ) { gentity_t *t = NULL; while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) ) GlobalUse( t, self, activator ); }
}

int main( void )
{
	int i, c;
	t_fresh(); t_init( 24 );
	for ( c = 0; c < 24; c++ ) t_player( c, va( "P%d", c ), qtrue, 100 + c );
	t_mapEntity( "func_button", "btn1", NULL ); t_mapEntity( "target_relay", "loop1", "loop2" ); t_mapEntity( "target_relay", "loop2", "loop1" );	// a use loop the chain limit cuts
	t_cs[0][0] = "sound/a.wav"; t_ncs[0] = 1; t_cs[1][0] = "env/fire"; t_ncs[1] = 1;
	t_mapEntity( "func_button", "chest_btn", "chest_relay" ); t_mapEntity( "target_relay", "chest_relay", "chest_pay" ); t_rewardEntity( "chest_pay", NULL, 100000, 0 );
	for ( c = 0; c < 200; c++ ) t_rewardEntity( "pay200", NULL, 100000, 0 );
	for ( c = 0; c < 200; c++ ) g_entities[t_mapEntity( "target_print", "print200", NULL )].use = s_HPrint;
	g_entities[t_mapEntity( "target_print", "bcast1", "bcast2" )].use = s_HPrint; g_entities[t_mapEntity( "target_print", "bcast2", "bcast1" )].use = s_HPrint;	// a loop: the chain limit ends it
	for ( i = 0; shapes[i].name; i++ )
	{
		rpLuaPlugin_t *p;
		int t0, took, k, maxq = 0;
		char code[4096];

		Com_sprintf( code, sizeof( code ), "Events.on('init', function() %s end)\n", shapes[i].code );
		t_write_plugin( "h", code );
		t_logLen = 0; t_log[0] = 0; t_clear_sent(); t_payouts = 0;
		t0 = trap->Milliseconds();
		t_start( "h" );
		took = trap->Milliseconds() - t0;
		p = RP_Lua_Find( "h" );
		for ( k = 0; k < 24; k++ ) if ( t_nsent[k] > maxq ) maxq = t_nsent[k];
		CHECK( took < RP_LUA_LIMIT_MS, "%s: init took %d ms", shapes[i].name, took );
		CHECK( maxq <= RP_LUA_CMDS_PER_SEC, "%s: %d commands to one player", shapes[i].name, maxq );
		CHECK( t_payouts <= RP_LUA_DB_WRITES_FRAME, "%s: %d payouts in one frame", shapes[i].name, t_payouts );
		if ( !strcmp( shapes[i].name, "pay200" ) ) CHECK( t_payouts == RP_LUA_DB_WRITES_FRAME, "pay200: the guard, not something else, stopped it (%d)", t_payouts );
		if ( !strcmp( shapes[i].name, "print200" ) ) CHECK( maxq == RP_LUA_CMDS_PER_SEC, "print200: %d reached the player", maxq );
		if ( !strcmp( shapes[i].name, "bcastloop" ) ) CHECK( maxq == RP_LUA_CMDS_PER_SEC, "bcastloop: %d reached each player", maxq );
		CHECK( p && p->inUse, "%s: plugin slot", shapes[i].name );
		if ( p ) CHECK( p->memUsed <= 512 * 1024, "%s: memory %d", shapes[i].name, (int)p->memUsed );
		t0 = trap->Milliseconds();
		for ( k = 0; k < 20; k++ ) { int before = t_payouts; t_frame( 50 ); CHECK( t_payouts - before <= RP_LUA_DB_WRITES_FRAME, "%s: %d payouts in a frame", shapes[i].name, t_payouts - before ); }	// timers and flushes
		took = trap->Milliseconds() - t0;
		CHECK( took < 20 * RP_LUA_LIMIT_MS, "%s: 20 frames took %d ms", shapes[i].name, took );
		for ( k = 0, maxq = 0; k < 24; k++ ) if ( t_nsent[k] > maxq ) maxq = t_nsent[k];
		CHECK( maxq < 129, "%s: %d commands queued to one player over a second", shapes[i].name, maxq );
		t0 = trap->Milliseconds();
		RP_LUA_SHUTDOWN();
		took = trap->Milliseconds() - t0;
		CHECK( took < RP_LUA_LIMIT_MS * 2, "%s: shutdown took %d ms", shapes[i].name, took );
		CHECK( rp_luaActive == 0 && rp_luaWatchCount == 0, "%s: clean after shutdown (%d %d)", shapes[i].name, rp_luaActive, rp_luaWatchCount );
		t_frame( 100 ); t_runThinks();
		{ int n, left = 0; for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) if ( g_entities[n].inuse && g_entities[n].rpLuaPlugin ) left++; CHECK( left == 0, "%s: %d script entities left", shapes[i].name, left ); }
		CHECK( t_useDepth == 0, "%s: use depth %d", shapes[i].name, t_useDepth );
		printf( "%-14s ok\n", shapes[i].name );
	}
	// every plugin slot at one player at once: what reaches that player is still the one budget
	{ char name[16], list[256]; int k, maxq = 0;
	  list[0] = 0; t_clear_sent(); t_frame( 1000 ); t_payouts = 0;
	  for ( k = 0; k < RP_LUA_MAX_PLUGINS; k++ ) { Com_sprintf( name, sizeof( name ), "m%d", k ); t_write_plugin( name, "Events.on('init', function() local p = Players.get(0) for i = 1, 100 do World.use('print200', p) World.use('pay200', p) Msg.print(p, 'x') end end)\n" ); Q_strcat( list, sizeof( list ), va( "%s ", name ) ); }
	  t_start( list ); CHECK( rp_luaActive == RP_LUA_MAX_PLUGINS, "all %d loaded: %d", RP_LUA_MAX_PLUGINS, rp_luaActive );
	  for ( k = 0; k < 24; k++ ) if ( t_nsent[k] > maxq ) maxq = t_nsent[k];
	  CHECK( maxq == RP_LUA_CMDS_PER_SEC && t_payouts == RP_LUA_DB_WRITES_FRAME, "16 plugins at one player: %d commands, %d payouts", maxq, t_payouts );
	  RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks(); printf( "%-14s ok\n", "allplugins" ); }

	// a plugin reloaded over and over while players ride what it spawns: never anyone thrown out, the released
	// ones bounded by the server's cap, all gone once everyone is off
	{ int r, n, t0 = trap->Milliseconds(), took, maxReleased = 0;
	  t_write_plugin( "h", "Events.on('init', function() for i = 1, 6 do World.vehicle('swoop', {x=0,y=0,z=0}) end end)\n" );
	  t_start( "h" );
	  for ( r = 0; r < 50; r++ )
	  {
		for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) if ( g_entities[n].inuse && g_entities[n].m_pVehicle && g_entities[n].rpLuaPlugin ) t_ride( &g_entities[n], &g_entities[r % 24], 0 );
		RP_Lua_QueueOp( "h", RPLOP_RELOAD ); t_frame( 50 ); t_runThinks();
		{ int rel = 0; for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) if ( g_entities[n].inuse && g_entities[n].rpLuaPlugin == RP_LUA_RELEASED ) rel++; if ( rel > maxReleased ) maxReleased = rel; }
	  }
	  took = trap->Milliseconds() - t0;
	  CHECK( maxReleased <= RP_LUA_VEHICLES_SERVER && t_freedRidden == 0 && took < 50 * RP_LUA_LIMIT_MS, "ridden reloads: %d released at most, %d thrown out, %d ms", maxReleased, t_freedRidden, took );
	  for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) if ( g_entities[n].inuse && g_entities[n].m_pVehicle ) t_ride( &g_entities[n], NULL, 0 );
	  t_frame( 600 ); t_frame( 50 ); t_frame( 150 ); t_runThinks();
	  { int rel = 0; for ( n = MAX_CLIENTS; n < level.num_entities; n++ ) if ( g_entities[n].inuse && g_entities[n].rpLuaPlugin == RP_LUA_RELEASED ) rel++; CHECK( rel == 0, "everyone off: %d released left", rel ); }
	  RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks();
	  CHECK( t_vehiclesAlive == 0 && t_freedRidden == 0, "all vehicles gone: %d", t_vehiclesAlive );
	  printf( "%-14s ok\n", "vehreload" ); }
	printf( fails ? "%d FAILED\n" : "t_hostile: ALL OK\n", fails );
	return fails != 0;
}
