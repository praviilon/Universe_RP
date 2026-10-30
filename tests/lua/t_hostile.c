// TEST ONLY: the worst shapes from the first attempt's audits, and what P2 reaches (rewards, chains of prints). Each must end within
// RP_LUA_LIMIT_MS of real time, drop nobody (an emulated engine queue drops a client 129 commands behind),
// and leave the state closable.
#include "t.h"

#define RP_LUA_LIMIT_MS 60

typedef struct { const char *name; const char *code; } shape_t;
static const char *get_str( rpLuaPlugin_t *p, const char *name ) { static char b[512]; lua_getglobal( p->L, name ); Q_strncpyz( b, lua_tostring( p->L, -1 ) ? lua_tostring( p->L, -1 ) : "(nil)", sizeof( b ) ); lua_pop( p->L, 1 ); return b; }
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
	// the audit's C-side loops (lstrlib, ltablib) and the escapes from the budget
	{ "tmove",		"table.move({}, 1, math.maxinteger - 1, 2)" },
	{ "tmovefwd",	"table.move({}, 1, math.maxinteger - 1, 1, {})" },
	{ "balance",	"local s = ('('):rep(250000) s:find('%b()')" },
	{ "linearpat",	"local s = ('a'):rep(200000) local p = ('a'):rep(100000) .. '%d' s:find(p)" },
	{ "plainfind",	"local s = ('a'):rep(200000) local p = ('a'):rep(100000) .. 'b' s:find(p, 1, true)" },
	{ "maxexpand",	"local s = ('a'):rep(200000) s:find('a*b')" },
	{ "emptyrep",	"local s = (''):rep(math.maxinteger) local t = (''):rep(math.maxinteger, '')" },
	{ "gmatchlong",	"local s = ('a'):rep(200000) local p = ('a'):rep(80000) .. 'b' for w in s:gmatch(p) do end" },
	{ "pcallloop",	"while true do pcall(function() while true do end end) end" },
	{ "pcallpat",	"local s = ('a'):rep(200000) while true do pcall(string.find, s, '.-.-.-.-x') end" },
	{ "gcloop",		"setmetatable({}, {__gc = function() while true do end end})" },
	{ "gcstore",	"setmetatable({}, {__gc = function() Storage.set('k', 'v') end})" },
	{ "gcworld",	"setmetatable({}, {__gc = function() World.use('x', Players.get(0)) end})" },
	{ "metaprop",	"local o o = setmetatable({}, {__index = function() return World.prop('fx_runner', o) end}) World.prop('fx_runner', o)" },
	{ "metanpc",	"local o = setmetatable({}, {__index = function() return 'enemy' end}) for i = 1, 100 do World.npc('jedi', {x=0,y=0,z=0}, nil, o) end" },
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
	{ "npcopts",	"local t = {'player', 'enemy', 'ghost', 'holo', 'x', 5, true, {}, 'fedora', 'none'} for i = 1, 100000 do pcall(World.npc, 'jedi', {x=0,y=0,z=0}, nil, {team=t[i%10+1], effect=t[(i*3)%10+1], health=i*7, scale=i%600, frozen=(i%2==0), noai=t[(i*7)%10+1], hat=t[(i*5)%10+1], cape=t[i%10+1]}) end" },
	{ "vehopts",	"local t = {'player', 'enemy', 'x', 5, true, {}} for i = 1, 100000 do pcall(World.vehicle, 'swoop', {x=0,y=0,z=0}, nil, {team=t[i%6+1], scale=i%600, health=(i%3==0) and 5 or nil}) end" },
	{ "questloop",	"for i = 1, 100000 do World.prop('target_dialog', {x=0,y=0,z=0}, nil, {message='x', option1='y', go1='pay'}) World.prop('target_reward', {x=0,y=0,z=0}, nil, {credits=100000}, {name='pay'}) World.prop('target_flag', {x=0,y=0,z=0}, nil, {flag='f'}) end" },
	{ "musicloop",	"local h = World.prop('target_play_music', {x=0,y=0,z=0}, nil, {music='music/x.mp3'}, {name='song'}) local p = Players.get(0) for i = 1, 100000 do World.use('lua.h.song', p) end" },
	// AUDIT 2 C4 / M-cost: the library's own loops, a lying __len, and the shapes that ran far past the budget at one step each
	{ "lenremove",	"local t = setmetatable({}, {__len = function() return math.maxinteger - 1 end}) local ok, e = pcall(table.remove, t, 1) WHY = tostring(e)" },
	{ "leninsert",	"local t = setmetatable({}, {__len = function() return math.maxinteger - 1 end}) local ok, e = pcall(table.insert, t, 1, 0) WHY = tostring(e)" },
	{ "lensort",	"local ok, e = pcall(table.sort, setmetatable({}, {__len = function() return 2147483000 end}), rawequal) WHY = tostring(e)" },
	{ "lenconcat",	"local ok, e = pcall(table.concat, setmetatable({}, {__len = function() return 1 << 30 end, __index = function() return 'x' end})) WHY = tostring(e)" },
	{ "bigremove",	"local t = {} for i = 1, 10000 do t[i] = i end for i = 1, 100000 do table.remove(t, 1) t[#t+1] = i end" },
	{ "biginsert",	"local t = {} for i = 1, 10000 do t[i] = i end for i = 1, 100000 do table.insert(t, 1, 0) t[#t] = nil end" },
	{ "bigsort",	"local t = {} for i = 1, 10000 do t[i] = (i * 7919) % 100003 end for k = 1, 1000 do table.sort(t) table.sort(t, function(a, b) return a > b end) end" },
	{ "bigconcat",	"local t = {} for i = 1, 20 do t[i] = ('x'):rep(5000) end for k = 1, 100000 do local x = table.concat(t, ('-'):rep(1000)) end" },
	{ "gsubrep",	"local s = ('a'):rep(5000) for k = 1, 100 do s:gsub('', ('%0'):rep(100000)) end" },
	{ "gsubbig",	"local s = ('a'):rep(1000) local r = ('r'):rep(200) for k = 1, 100000 do local x = s:gsub('a', r) end" },
	{ "bigclass",	"local s = ('a'):rep(5000) local c = '[' .. ('b'):rep(200000) .. 'a]+x' for k = 1, 100 do s:find(c) end" },
	{ "frontier",	"local s = ('a'):rep(5000) local c = '%f[' .. ('b'):rep(200000) .. 'a]x' for k = 1, 100 do s:find(c) end" },
	{ "bigfind",	"local s = ('a'):rep(100000) local n = ('a'):rep(50000) .. 'b' for k = 1, 100000 do s:find(n, 1, true) end" },
	{ "bigrep",		"for k = 1, 100000 do local x = ('x'):rep(100, ('-'):rep(1000)) end" },
	{ "musictimer",	"World.prop('target_play_music', {x=0,y=0,z=0}, nil, {music='music/x.mp3'}, {name='song'}) Timer.every(50, function() for i = 0, 23 do World.use('lua.h.song', Players.get(i)) end end)" },
	{ "linkbomb",	"for i = 1, 100000 do pcall(World.prop, 'misc_model_breakable', {x=0,y=0,z=0}, nil, {paintarget=('a'):rep(i % 80), target='door.1', go1='x'}) end" },
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
		t_logLen = 0; t_log[0] = 0; t_clear_sent(); t_payouts = 0; t_musicChanges = 0;
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
		if ( !strcmp( shapes[i].name, "musicloop" ) ) CHECK( t_musicChanges <= RP_LUA_CMDS_PER_SEC && t_musicChanges >= 1, "musicloop: %d music changes in one call", t_musicChanges );
		if ( ( !strncmp( shapes[i].name, "big", 3 ) && strcmp( shapes[i].name, "bigvalue" ) ) || !strcmp( shapes[i].name, "gsubrep" ) || !strcmp( shapes[i].name, "gsubbig" ) || !strcmp( shapes[i].name, "frontier" ) ) CHECK( t_log_has( "ran too long" ), "%s: the budget, not the end of the loop, stopped it", shapes[i].name );
		if ( !strncmp( shapes[i].name, "len", 3 ) ) CHECK( p && p->L && strstr( get_str( p, "WHY" ), "table too big" ), "%s: refused before any loop (%s)", shapes[i].name, p && p->L ? get_str( p, "WHY" ) : "-" );
		if ( !strcmp( shapes[i].name, "usechain" ) ) CHECK( p && p->chainsCut >= 1 && t_useRefused == 0, "usechain: the script chain cap stopped the loop %d times, the 2048 cap never reached", p ? p->chainsCut : -1 );	// AUDIT 2 C5
		if ( !strcmp( shapes[i].name, "questloop" ) ) CHECK( p && p->dialogSpawns <= RP_LUA_DIALOG_SPAWNS_MAP && p->propSpawns <= RP_LUA_TOKENS, "questloop: %d dialogues, %d props (the bucket paces it)", p ? p->dialogSpawns : -1, p ? p->propSpawns : -1 );
		CHECK( p && p->inUse, "%s: plugin slot", shapes[i].name );
		if ( p ) CHECK( p->memUsed <= 512 * 1024, "%s: memory %d", shapes[i].name, (int)p->memUsed );
		t0 = trap->Milliseconds();
		for ( k = 0; k < 20; k++ ) { int before = t_payouts, mbefore = t_musicChanges; t_frame( 50 ); CHECK( t_payouts - before <= RP_LUA_DB_WRITES_FRAME, "%s: %d payouts in a frame", shapes[i].name, t_payouts - before ); CHECK( t_musicChanges - mbefore <= RP_LUA_TOKENS, "%s: %d music changes in a frame", shapes[i].name, t_musicChanges - mbefore ); }	// timers and flushes
		if ( !strcmp( shapes[i].name, "musictimer" ) ) CHECK( t_musicChanges <= RP_LUA_CMDS_PER_SEC, "musictimer: %d music changes in a second", t_musicChanges );
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
	// AUDIT 2 C4, deterministic: with the clock frozen every read is a millisecond, so ONE library call whose own checks read
	// the clock 11+ times is killed "in a library loop" whatever the machine's speed; without the check (or the weight)
	// the call ends on its own and nothing is logged
	{ static const char *lib[][2] = {
	    { "rm",     "T = {} for i = 1, 12000 do T[i] = i end Commands.add('golib', function() table.remove(T, 1) end)" },
	    { "ins",    "T = {} for i = 1, 12000 do T[i] = i end Commands.add('golib', function() table.insert(T, 1, 0) end)" },
	    { "sort",   "T = {} for i = 1, 12000 do T[i] = (i * 7919) % 100003 end Commands.add('golib', function() table.sort(T) end)" },
	    { "concat", "local piece = ('p'):rep(500) T = {} for i = 1, 500 do T[i] = piece end Commands.add('golib', function() local x = table.concat(T) end)" },	// 500 pieces of 500 bytes: the piece weight, not the separator's
	    { "concsep","T = {} for i = 1, 200 do T[i] = 'abcdefghij' end SEP = ('-'):rep(1000) Commands.add('golib', function() local x = table.concat(T, SEP) end)" },
	    // AUDIT 3 C1: equal keys compare false both ways (the partition's own loops never ran), and a string
	    // comparison is strcoll over the whole string; each level of the recursion had its own counter
	    { "sortstr","local s = ('s'):rep(20000) T = {} for i = 1, 2000 do T[i] = s end Commands.add('golib', function() table.sort(T) end)" },
	    { "sorteq", "T = {} for i = 1, 16384 do T[i] = 7 end Commands.add('golib', function() table.sort(T) end)" },
	    // few comparisons (40 entries: about 200), each strcoll over 6 KB: only the weight makes them checks
	    { "sortlong","local s = ('s'):rep(6000) T = {} for i = 1, 40 do T[i] = s .. string.char(65 + (i * 7) % 40) end Commands.add('golib', function() table.sort(T) end)" },
	    { "find",   "local a = ('a'):rep(4000) S = ('x' .. a):rep(60) N = 'x' .. a .. 'b' Commands.add('golib', function() S:find(N, 1, true) end)" },	// 59 candidates, 4 KB compared each
	    { "rep",    "SEP = ('-'):rep(8000) Commands.add('golib', function() local x = ('x'):rep(30, SEP) end)" },
	    // AUDIT 4: a bracket class costs its length at every character it is tried on (about 800 tries of a 20 KB class
	    // here); weighted, those are checks; counted as 1 each the call never reads the clock (the timed "bigclass" shape
	    // above caught that only by a few milliseconds)
	    { "class",  "S = ('a'):rep(40) C = '[' .. ('b'):rep(20000) .. 'a]+x' Commands.add('golib', function() S:find(C) end)" },
	    { NULL, NULL } };
	  int i;
	  for ( i = 0; lib[i][0]; i++ )
	  {
		rpLuaPlugin_t *p;
		t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_write_plugin( "lib", lib[i][1] ); t_start( "lib" ); p = RP_Lua_Find( "lib" );
		CHECK( p && p->L && p->numCommands == 1, "%s: set up (%s)", lib[i][0], p && p->L ? "state" : strstr( t_log, "error" ) ? strstr( t_log, "error" ) : t_log );
		t_frame( 100 );	// a fresh frame budget: the call gets the full RP_LUA_CALL_MS, not the short deadline
		t_logLen = 0; t_log[0] = 0; t_clockFrozen = qtrue; t_tick = 1;
		t_args( "golib" ); RP_LUA_COMMAND( &g_entities[0], t_argv[0] );
		t_clockFrozen = qfalse; t_tick = 0;
		CHECK( t_log_has( "in a library loop" ), "%s: one call, killed by the library's own checks (%s) deadline %d ms", lib[i][0], strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "no error", p->deadlineMs );
		CHECK( p->deadlineMs == RP_LUA_CALL_MS, "%s: the full deadline (%d)", lib[i][0], p->deadlineMs );
		RP_LUA_SHUTDOWN();
	  } }
	// AUDIT 3 C1: the counted sort still sorts, with and without a function, numbers and strings, on the real clock
	{ rpLuaPlugin_t *p;
	  t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	  t_write_plugin( "srt", "OK = true\n"
		"Commands.add('srtn', function() local n = {} for i = 1, 600 do n[i] = (i * 7919) % 601 end table.sort(n) for i = 2, #n do if n[i - 1] > n[i] then OK = false end end end)\n"
		"Commands.add('srts', function() local s = {} for i = 1, 300 do s[i] = 'k' .. ((i * 61) % 301) end table.sort(s) for i = 2, #s do if s[i - 1] > s[i] then OK = false end end end)\n"
		"Commands.add('srtd', function() local d = {} for i = 1, 300 do d[i] = (i * 31) % 299 end table.sort(d, function(a, b) return a > b end) for i = 2, #d do if d[i - 1] < d[i] then OK = false end end end)\n"
		"Commands.add('srte', function() local e = {} for i = 1, 300 do e[i] = 3 end table.sort(e) for i = 1, #e do if e[i] ~= 3 then OK = false end end end)\n" );
	  t_start( "srt" ); p = RP_Lua_Find( "srt" ); t_logLen = 0; t_log[0] = 0;
	  { static const char *c[] = { "srtn", "srts", "srtd", "srte" }; int k; for ( k = 0; k < 4; k++ ) { t_args( c[k] ); RP_LUA_COMMAND( &g_entities[0], c[k] ); t_frame( 50 ); } }
	  lua_getglobal( p->L, "OK" ); CHECK( lua_toboolean( p->L, -1 ) && !t_log_has( "error" ), "ordinary sorts are sorted and within the budget (%s)", strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" ); lua_pop( p->L, 1 );
	  RP_LUA_SHUTDOWN(); }
	t_init( 8 );	// TEST ONLY: frees what the stubs still hold (spawned strings, NPC clients): valgrind ends with nothing reachable
	printf( fails ? "%d FAILED\n" : "t_hostile: ALL OK\n", fails );
	return fails != 0;
}
