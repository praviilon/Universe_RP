// TEST ONLY: the bucket, messages, commands, players, events, watches
#include "t.h"

static int get_int( rpLuaPlugin_t *p, const char *name ) { int v; lua_getglobal( p->L, name ); v = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); return v; }

int main( void )
{
	rpLuaPlugin_t *p;
	int i;
	t_fresh(); t_init( 8 );
	t_player( 0, "^1Alice", qtrue, 11 ); t_player( 1, "Bob", qfalse, 0 ); t_player( 2, "Carol", qtrue, 12 );
	g_entities[0].client->pers.bitvalue = 1 << 29;

	// --- the bucket: 4 prints, then "too fast", a token back after sv_floodProtect
	t_write_plugin( "b", "N = 0 WHY = '' Commands.add('spam', function(p, args) for i = 1, 10 do local ok, why = Msg.print(p, 'line ' .. i) if ok then N = N + 1 else WHY = why end end end, 'spams')\n"
		"Commands.add('bcast', function(p) OK, BWHY = Msg.broadcast('hello all') end)\n"
		"Commands.add('loop', function(p) local n = 0 for i = 1, 10000 do if Msg.print(p, 'x') then n = n + 1 end end N = n end)\n"
		"Commands.add('args', function(p, args) A = #args .. ':' .. (args[1] or '') end)\n" );
	t_start( "b" ); p = RP_Lua_Find( "b" ); CHECK( p && p->L, "b loaded" );
	t_args( "spam" ); CHECK( RP_LUA_COMMAND( &g_entities[0], "spam" ), "command found" );
	CHECK( get_int( p, "N" ) == 4 && t_sent_count( 0 ) == 4, "4 of 10 sent: N %d sent %d", get_int( p, "N" ), t_sent_count( 0 ) );
	{ lua_getglobal( p->L, "WHY" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "too fast" ) || !strcmp( lua_tostring( p->L, -1 ), "player busy" ), "why: %s", lua_tostring( p->L, -1 ) ); /* one recipient: both limits are 4 */ lua_pop( p->L, 1 ); }
	CHECK( strstr( t_sent[0][0], "print \"line 1\n\"" ) != NULL, "print command: %s", t_sent[0][0] );
	t_clear_sent(); t_frame( 500 ); t_args( "spam" ); RP_LUA_COMMAND( &g_entities[0], "spam" ); CHECK( t_sent_count( 0 ) == 0, "no token after 500 ms: %d", t_sent_count( 0 ) );
	t_frame( 600 ); t_args( "spam" ); RP_LUA_COMMAND( &g_entities[0], "spam" ); CHECK( t_sent_count( 0 ) == 1, "one token after 1.1 s: %d", t_sent_count( 0 ) );
	// the loop of 10000 ends quickly and sends what the bucket allows
	for ( i = 0; i < 5; i++ ) t_frame( 1000 ); t_clear_sent();
	{ int t0 = trap->Milliseconds(); t_args( "loop" ); RP_LUA_COMMAND( &g_entities[0], "loop" ); CHECK( trap->Milliseconds() - t0 < 50, "loop of 10000 prints took %d ms", trap->Milliseconds() - t0 ); }
	CHECK( t_sent_count( 0 ) == 4 && ( get_int( p, "N" ) == 4 || p->errorsTotal == 1 ), "loop sent 4: %d (N %d, errors %d: under ASan the 10000 may run past 10 ms, which is the budget working)", t_sent_count( 0 ), get_int( p, "N" ), p->errorsTotal );
	// the per-recipient ceiling: a second plugin cannot push past 4 a second to the same player
	t_write_plugin( "c", "Commands.add('spam2', function(p) OK2, WHY2 = Msg.print(p, 'more') end)\n" ); RP_Lua_QueueLoad( "c" ); t_frame( 10 );
	{ rpLuaPlugin_t *c = RP_Lua_Find( "c" ); CHECK( c && c->L, "c loaded" ); t_args( "spam2" ); RP_LUA_COMMAND( &g_entities[0], "spam2" ); lua_getglobal( c->L, "WHY2" ); CHECK( !strcmp( lua_tostring( c->L, -1 ) ? lua_tostring( c->L, -1 ) : "", "player busy" ), "recipient ceiling: %s", lua_tostring( c->L, -1 ) ); lua_pop( c->L, 1 ); CHECK( c->refusedRecipients == 1, "counted" ); }
	// broadcast: 3 players = 1 token
	for ( i = 0; i < 5; i++ ) t_frame( 1000 ); t_clear_sent();
	t_args( "bcast" ); RP_LUA_COMMAND( &g_entities[0], "bcast" ); CHECK( t_sent_count( 0 ) == 1 && t_sent_count( 1 ) == 1 && t_sent_count( 2 ) == 1 && t_sent_count( 3 ) == 0 && p->tokens == 3, "broadcast to 3 for 1 token (tokens %d)", p->tokens );
	// args
	t_args( "args one two" ); RP_LUA_COMMAND( &g_entities[0], "args" ); { lua_getglobal( p->L, "A" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "2:one" ), "args: %s", lua_tostring( p->L, -1 ) ); lua_pop( p->L, 1 ); }
	// a built-in name is refused, a taken name too
	t_args( "lua R1, W1 = Commands.add('say', function() end) R2, W2 = Commands.add('spam', function() end) return tostring(R1) .. ' ' .. tostring(R2) .. ' ' .. tostring(W2)" ); t_console[0] = 0; Svcmd_Lua_f();
	CHECK( strstr( t_console, "false false plugin b has that command" ) != NULL, "name refusals: %s", t_console );
	CHECK( !RP_LUA_COMMAND( &g_entities[0], "nosuch" ), "unknown command stays unknown" );
	RP_LUA_SHUTDOWN();

	// --- players and events
	t_init( 8 ); t_player( 0, "^1Alice", qtrue, 11 ); t_player( 1, "Bob", qfalse, 0 ); t_player( 2, "Carol", qtrue, 12 ); g_entities[2].client->ps.origin[0] = 500; g_entities[0].client->pers.bitvalue = 1 << 29;
	t_write_plugin( "ev", "L = {} Events.on('client_begin', function(e) L[#L+1] = 'begin ' .. e.player.clean_name .. ' ' .. e.player.id end)\n"
		"Events.on('client_disconnect', function(e) LEFT = e.player.name HELD = e.player end)\n"
		"Events.on('chat', function(e) L[#L+1] = 'chat ' .. e.mode .. ' ' .. e.text .. (e.target and (' to ' .. e.target.id) or '') end)\n"
		"Events.on('player_death', function(e) L[#L+1] = 'death ' .. e.victim.id .. ' ' .. (e.killer and e.killer.id or -1) .. ' ' .. e.mod end)\n"
		"Events.on('login', function(e) L[#L+1] = 'login ' .. e.account end) Events.on('level_up', function(e) L[#L+1] = 'level ' .. e.level end) Events.on('character_select', function(e) L[#L+1] = 'char ' .. e.char_id end)\n"
		"World.watch('door1') Events.on('entity_used', function(e) L[#L+1] = 'used ' .. e.targetname .. ' ' .. e.entity.classname .. ' ' .. (e.activator and e.activator.id or -1) end)\n"
		"Commands.add('who', function(p) local t = Players.all() N = #t local near = World.players_within({x=0,y=0,z=0}, 100) NEAR = #near ADM = tostring(p:is_admin('Scripts')) LVL = p.level CR = p.credits ACC = tostring(p.account) T = p.team B = tostring(p.is_bot) G = tostring(Players.get(1) == Players.get(1)) end)\n" );
	t_start( "ev" ); p = RP_Lua_Find( "ev" ); CHECK( p && p->L, "ev loaded" ); CHECK( rp_luaWatchCount == 1, "watch counted" );
	RP_LUA_EVENT_BEGIN( &g_entities[0] ); RP_LUA_EVENT_CHAT( &g_entities[0], NULL, SAY_OOC, "hi ^2there" ); RP_LUA_EVENT_CHAT( &g_entities[0], &g_entities[2], SAY_TELL, "psst" ); RP_LUA_EVENT_DEATH( &g_entities[2], &g_entities[0], 7 );
	RP_LUA_EVENT_ACCOUNT( &g_entities[0], "login", 0 ); RP_LUA_EVENT_ACCOUNT( &g_entities[0], "level_up", 6 ); RP_LUA_EVENT_ACCOUNT( &g_entities[0], "character_select", 11 );
	g_entities[40].inuse = qtrue; g_entities[40].targetname = "door1"; g_entities[40].classname = "func_door"; level.num_entities = 41;
	RP_LUA_EVENT_USED( &g_entities[40], &g_entities[0] ); g_entities[41].inuse = qtrue; g_entities[41].targetname = "other"; g_entities[41].classname = "func_button"; RP_LUA_EVENT_USED( &g_entities[41], &g_entities[0] );
	{ const char *exp[] = { "begin Alice 0", "chat ooc hi ^2there", "chat tell psst to 2", "death 2 0 7", "login acc0", "level 6", "char 11", "used door1 func_door 0", NULL }; int k; lua_getglobal( p->L, "L" ); CHECK( (int)lua_rawlen( p->L, -1 ) == 8, "8 events logged: %d", (int)lua_rawlen( p->L, -1 ) ); for ( k = 0; exp[k]; k++ ) { lua_rawgeti( p->L, -1, k + 1 ); CHECK( lua_tostring( p->L, -1 ) && !strcmp( lua_tostring( p->L, -1 ), exp[k] ), "event %d: %s", k, lua_tostring( p->L, -1 ) ); lua_pop( p->L, 1 ); } lua_pop( p->L, 1 ); }
	t_args( "who" ); RP_LUA_COMMAND( &g_entities[0], "who" );
	CHECK( get_int( p, "N" ) == 3 && get_int( p, "NEAR" ) == 2 && get_int( p, "LVL" ) == 5 && get_int( p, "CR" ) == 100, "players: N %d near %d", get_int( p, "N" ), get_int( p, "NEAR" ) );
	{ lua_getglobal( p->L, "ADM" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "true" ), "is_admin" ); lua_pop( p->L, 1 ); lua_getglobal( p->L, "ACC" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "acc0" ), "account" ); lua_pop( p->L, 1 ); lua_getglobal( p->L, "G" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "true" ), "handle equality" ); lua_pop( p->L, 1 ); }
	// a stale handle: disconnect, then the slot is reused
	RP_LUA_EVENT_DISCONNECT( &g_entities[1] ); RP_LUA_GEN( 1 ); g_entities[1].client->pers.connected = CON_DISCONNECTED; t_player( 1, "Dave", qfalse, 0 );
	t_args( "lua return tostring(HELD:valid()) .. ' ' .. tostring(HELD.name) .. ' ' .. HELD.id .. ' ' .. tostring(LEFT)" ); // HELD lives in ev's state, not rcon's: check through ev instead
	lua_getglobal( p->L, "HELD" ); { int cn; lua_getfield( p->L, -1, "valid" ); lua_pushvalue( p->L, -2 ); lua_call( p->L, 1, 1 ); CHECK( !lua_toboolean( p->L, -1 ), "stale handle invalid" ); lua_pop( p->L, 1 ); lua_getfield( p->L, -1, "name" ); CHECK( lua_isnil( p->L, -1 ), "stale name nil" ); lua_pop( p->L, 1 ); lua_getfield( p->L, -1, "id" ); cn = (int)lua_tointeger( p->L, -1 ); CHECK( cn == 1, "stale id kept %d", cn ); lua_pop( p->L, 2 ); }
	lua_getglobal( p->L, "LEFT" ); CHECK( !strcmp( lua_tostring( p->L, -1 ), "Bob" ), "disconnect event saw the name" ); lua_pop( p->L, 1 );
	// a message to a stale handle raises, counted as the plugin's error, and the plugin lives
	t_write_plugin( "st", "Events.on('client_disconnect', function(e) H = e.player end) Commands.add('late', function(p) Msg.print(H, 'x') end)\n" ); RP_Lua_QueueLoad( "st" ); t_frame( 10 );
	{ rpLuaPlugin_t *s = RP_Lua_Find( "st" ); RP_LUA_EVENT_DISCONNECT( &g_entities[2] ); RP_LUA_GEN( 2 ); t_args( "late" ); RP_LUA_COMMAND( &g_entities[0], "late" ); CHECK( s->errorsTotal == 1 && s->L, "stale send is an error: %d", s->errorsTotal ); CHECK( t_log_has( "the player has left" ), "message" ); }
	// too many watches, too many timers, too many commands
	t_args( "lua local n = 0 for i = 1, 40 do if World.watch('w' .. i) then n = n + 1 end end local m = 0 for i = 1, 40 do if Timer.after(100, function() end) then m = m + 1 end end return n, m" ); t_console[0] = 0; Svcmd_Lua_f();
	CHECK( strstr( t_console, "32\t32" ) != NULL, "caps: %s", t_console ); CHECK( rp_luaWatchCount == 1, "scratch watches gone: %d", rp_luaWatchCount );
	// Cvar.get only rp_/g_/sv_
	t_args( "lua local ok = pcall(Cvar.get, 'fs_homepath') return tostring(ok), Cvar.get('sv_floodProtect')" ); t_console[0] = 0; Svcmd_Lua_f(); CHECK( strstr( t_console, "false\t" ) != NULL, "cvar guard: %s", t_console );
	// text limits: 901 bytes raise; quotes are swapped
	for ( i = 0; i < 5; i++ ) t_frame( 1000 ); t_clear_sent();
	t_write_plugin( "tx", "Commands.add('txt', function(p) OK = pcall(Msg.print, p, ('a'):rep(901)) R, W = Msg.print(p, 'say \"hi\"') end)\n" ); RP_Lua_QueueLoad( "tx" ); t_frame( 10 );
	t_args( "txt" ); RP_LUA_COMMAND( &g_entities[0], "txt" ); { rpLuaPlugin_t *x = RP_Lua_Find( "tx" ); lua_getglobal( x->L, "OK" ); CHECK( !lua_toboolean( x->L, -1 ), "901 bytes raise" ); lua_pop( x->L, 1 ); } CHECK( t_sent_count( 0 ) == 1 && strstr( t_sent[0][0], "say 'hi'" ) != NULL, "quotes swapped: count %d first %s why %s", t_sent_count( 0 ), t_sent[0][0], ( lua_getglobal( RP_Lua_Find( "tx" )->L, "W" ), lua_tostring( RP_Lua_Find( "tx" )->L, -1 ) ) );
	// /scripts needs the power; lists
	for ( i = 0; i < 5; i++ ) t_frame( 1000 ); t_clear_sent(); t_args( "scripts list" ); Cmd_Scripts_f( &g_entities[0] ); CHECK( t_sent_count( 0 ) >= 2 && strstr( t_sent[0][1], "^3ev^7: running" ) != NULL, "scripts list: %s", t_sent[0][1] );
	t_clear_sent(); Cmd_Scripts_f( &g_entities[1] ); CHECK( t_sent_count( 1 ) == 0, "no power: nothing (check_admin_command stub is silent)" );
	RP_LUA_SHUTDOWN();

	// --- AUDIT: the command cap, the timer minimum, the guard's ring size and broadcast counting
	t_init( 8 ); t_player( 5, "Eve", qtrue, 9 );
	t_write_plugin( "cap", "N = 0 for i = 1, 20 do if Commands.add('cap' .. i, function() end) then N = N + 1 end end\nT = 0 Timer.after(1, function() T = 1 end)\n" );
	t_start( "cap" ); p = RP_Lua_Find( "cap" );
	CHECK( p && p->L && get_int( p, "N" ) == RP_LUA_MAX_COMMANDS && p->numCommands == RP_LUA_MAX_COMMANDS, "commands capped at %d: %d", RP_LUA_MAX_COMMANDS, p && p->L ? get_int( p, "N" ) : -1 );
	t_frame( 10 ); t_frame( 10 ); t_frame( 10 ); CHECK( p && p->L && get_int( p, "T" ) == 0, "a 1 ms timer waits the minimum" );
	t_frame( RP_LUA_TIMER_MIN_MS ); t_frame( RP_LUA_TIMER_MIN_MS ); CHECK( p && p->L && get_int( p, "T" ) == 1, "and then runs" );
	rp_luaInCall++;
	CHECK( !RP_Lua_ScriptCmdRoom( 5, RP_LUA_CMDS_PER_SEC + 1 ), "more than the ring holds never fits" );
	{ int k, ok = 1; for ( k = 0; k < RP_LUA_CMDS_PER_SEC; k++ ) if ( !RP_Lua_ScriptBroadcastRoom( 1 ) ) ok = 0;
	  CHECK( ok && !RP_Lua_ScriptBroadcastRoom( 1 ), "a broadcast that passes is counted: %d pass, then none", RP_LUA_CMDS_PER_SEC ); }
	rp_luaInCall--;
	RP_LUA_SHUTDOWN();

	// --- target_lua: the map entity that loads, unloads or reloads a plugin as /scripts does, queued for the frame end;
	// spawn checks; ONCE and INACTIVE; refused from inside a script call and skipped by World.use; rp_lua off logs only
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "qst", "Events.on('init', function() Log.info('qst up') end)\n" );
	t_write_plugin( "usr", "Commands.add('poke', function(p, a) U, S = World.use(a[1], p) end)\n" );
	t_start( "usr" ); p = RP_Lua_Find( "usr" ); CHECK( p && p->L, "usr loaded" );
	{ gentity_t *e; int tl, tlOnce, tlOff, k;
	  // spawn checks
	  t_keysClear(); e = &g_entities[t_mapEntity( "target_lua", "nolua", NULL )]; SP_target_lua( e ); CHECK( !e->inuse && strstr( level.rp_spawn_refusal, "plugin name" ), "no plugin key: refused (%s)", level.rp_spawn_refusal );
	  t_keysClear(); t_key( "plugin", "../x" ); e = &g_entities[t_mapEntity( "target_lua", "badlua", NULL )]; SP_target_lua( e ); CHECK( !e->inuse, "a bad plugin name refused" );
	  t_keysClear(); t_key( "plugin", "qst" ); t_key( "action", "explode" ); e = &g_entities[t_mapEntity( "target_lua", "badact", NULL )]; SP_target_lua( e ); CHECK( !e->inuse && strstr( level.rp_spawn_refusal, "action" ), "a bad action refused (%s)", level.rp_spawn_refusal );
	  t_esMade = qtrue; t_keysClear(); t_key( "plugin", "nosuch" ); e = &g_entities[t_mapEntity( "target_lua", "nofile", NULL )]; SP_target_lua( e ); CHECK( !e->inuse && strstr( level.rp_spawn_refusal, "no plugin" ), "entity commands: a missing file refused (%s)", level.rp_spawn_refusal ); t_esMade = qfalse;
	  t_keysClear(); t_key( "plugin", "nosuch" ); e = &g_entities[t_mapEntity( "target_lua", "mapnofile", NULL )]; SP_target_lua( e ); CHECK( e->inuse && e->use == target_lua_use, "the map's own with a missing file spawns (logged at use)" );
	  // the real one
	  t_keysClear(); t_key( "plugin", "qst" ); tl = t_mapEntity( "target_lua", "ql", NULL ); e = &g_entities[tl]; e->use = NULL; SP_target_lua( e ); CHECK( e->inuse && e->use == target_lua_use && e->count == 0 && !strcmp( e->message, "qst" ), "target_lua ql spawned (load qst)" );
	  t_keysClear(); t_key( "plugin", "qst" ); t_key( "action", "unload" ); tlOnce = t_mapEntity( "target_lua", "qu", NULL ); e = &g_entities[tlOnce]; e->spawnflags = 1; e->use = NULL; SP_target_lua( e ); CHECK( e->inuse && e->count == 1, "qu: unload, ONCE" );
	  t_keysClear(); t_key( "plugin", "qst" ); tlOff = t_mapEntity( "target_lua", "qi", NULL ); e = &g_entities[tlOff]; e->spawnflags = 128; e->use = NULL; SP_target_lua( e ); CHECK( e->inuse && ( e->flags & FL_INACTIVE ), "qi: INACTIVE" );
	  // a player's use (a button): queued, loaded at the frame end, with the plugin's own init
	  t_logLen = 0; t_log[0] = 0; GlobalUse( &g_entities[tl], &g_entities[0], &g_entities[0] );
	  CHECK( !RP_Lua_Find( "qst" ) && t_log_has( "used by Alice" ) && t_log_has( "Loading qst" ), "used: queued, not loaded yet (%s)", t_log );
	  t_frame( 50 ); CHECK( RP_Lua_Find( "qst" ) && RP_Lua_Find( "qst" )->L && t_log_has( "[lua:qst] qst up" ), "loaded at the frame end, init ran" );
	  GlobalUse( &g_entities[tl], &g_entities[0], &g_entities[0] ); CHECK( t_log_has( "already running" ), "used again: already running" );
	  // from inside a script call: refused and counted, not queued; World.use skips the class
	  t_logLen = 0; t_log[0] = 0; p->tokens = RP_LUA_TOKENS; t_args( "poke ql" ); RP_LUA_COMMAND( &g_entities[0], "poke" );
	  CHECK( get_int( p, "U" ) == 0 && get_int( p, "S" ) == 1 && !t_log_has( "target_lua" ), "World.use skips a target_lua (U %d S %d)", get_int( p, "U" ), get_int( p, "S" ) );
	  rp_luaInCall++; rpLuaCurrent = p; GlobalUse( &g_entities[tlOnce], &g_entities[0], &g_entities[0] ); rpLuaCurrent = NULL; rp_luaInCall--;
	  CHECK( t_log_has( "refused: used from a script (usr)" ) && p->luaUsesHeld == 1 && RP_Lua_Find( "qst" )->pendingOp == RPLOP_NONE && !g_entities[tlOnce].genericValue1, "a chain inside a script call: refused, counted, ONCE not spent" );
	  // INACTIVE does nothing; ONCE does its unload once
	  GlobalUse( &g_entities[tlOff], &g_entities[0], &g_entities[0] ); CHECK( RP_Lua_Find( "qst" )->pendingOp == RPLOP_NONE, "inactive: nothing" );
	  GlobalUse( &g_entities[tlOnce], &g_entities[0], &g_entities[0] ); CHECK( RP_Lua_Find( "qst" )->pendingOp == RPLOP_UNLOAD && t_log_has( "will be unloaded" ), "qu: unload queued" );
	  t_frame( 50 ); CHECK( !RP_Lua_Find( "qst" ), "unloaded at the frame end" );
	  GlobalUse( &g_entities[tl], &g_entities[0], &g_entities[0] ); t_frame( 50 ); CHECK( RP_Lua_Find( "qst" ) && RP_Lua_Find( "qst" )->L, "loaded again by ql" );
	  GlobalUse( &g_entities[tlOnce], &g_entities[0], &g_entities[0] ); CHECK( RP_Lua_Find( "qst" )->pendingOp == RPLOP_NONE, "qu is ONCE: the second use does nothing" );
	  // reload
	  t_keysClear(); t_key( "plugin", "qst" ); t_key( "action", "reload" ); k = t_mapEntity( "target_lua", "qr", NULL ); e = &g_entities[k]; e->use = NULL; SP_target_lua( e );
	  { lua_State *L0 = RP_Lua_Find( "qst" )->L; GlobalUse( e, &g_entities[0], &g_entities[0] ); CHECK( RP_Lua_Find( "qst" )->pendingOp == RPLOP_RELOAD, "qr: reload queued" ); t_frame( 50 ); CHECK( RP_Lua_Find( "qst" )->L && RP_Lua_Find( "qst" )->L != L0, "reloaded: a fresh state" ); }
	  // the map's own with a missing file: the load fails at the frame end, logged, nothing else
	  t_logLen = 0; t_log[0] = 0;
	  { gentity_t *m = NULL; while ( ( m = G_Find( m, FOFS( targetname ), "mapnofile" ) ) ) GlobalUse( m, &g_entities[0], &g_entities[0] ); } t_frame( 50 ); CHECK( t_log_has( "nosuch not loaded" ) && !RP_Lua_Find( "nosuch" ), "missing file: logged at the frame end" );
	  // rp_lua off: logs, does nothing
	  rp_lua.integer = 0; t_logLen = 0; t_log[0] = 0; GlobalUse( &g_entities[tl], &g_entities[0], &g_entities[0] ); CHECK( t_log_has( "rp_lua is off" ), "rp_lua off: logged" ); rp_lua.integer = 1; }
	RP_LUA_SHUTDOWN();

	printf( fails ? "%d FAILED\n" : "t_api: ALL OK\n", fails );
	return fails != 0;
}
