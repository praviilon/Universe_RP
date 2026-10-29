// TEST ONLY: P2 -- World.use through GlobalUse (locks, skips, counts, no self-event), World.prop and
// World.npc (classes, keys, names, caps, refusals, tags, handles), World.vehicle (types, caps, ridden ones
// never freed: released at a close, swept once empty), World.remove queued, unload cleanup,
// Effect/Sound lookup-only, every action a token.
#include "t.h"

static int get_int( rpLuaPlugin_t *p, const char *name ) { int v; lua_getglobal( p->L, name ); v = lua_isboolean( p->L, -1 ) ? lua_toboolean( p->L, -1 ) : (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); return v; }
static const char *get_str( rpLuaPlugin_t *p, const char *name ) { static char b[512]; lua_getglobal( p->L, name ); Q_strncpyz( b, lua_tostring( p->L, -1 ) ? lua_tostring( p->L, -1 ) : "(nil)", sizeof( b ) ); lua_pop( p->L, 1 ); return b; }
static void run( rpLuaPlugin_t *p, const char *cmd ) { (void)p; t_args( cmd ); RP_LUA_COMMAND( &g_entities[0], t_argv[0] ); }
static void refill( rpLuaPlugin_t *p ) { p->tokens = RP_LUA_TOKENS; }
// every live vehicle with this tag gets that pilot (NULL: everyone off)
static void ride_all( int tag, gentity_t *pilot ) { int i; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].m_pVehicle && g_entities[i].rpLuaPlugin == tag ) t_ride( &g_entities[i], pilot, 0 ); }
static int count_tag( int tag ) { int i, n = 0; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].rpLuaPlugin == tag ) n++; return n; }

// a stand-in target_print: one command to its activator, or to everyone; s_test_limit > 0: only that many do
static int s_test_broadcast, s_test_limit, s_test_count;
static void s_test_print( gentity_t *self, gentity_t *other, gentity_t *activator )
{
	(void)self; (void)other;
	if ( s_test_limit && s_test_count++ >= s_test_limit ) return;
	trap->SendServerCommand( s_test_broadcast ? -1 : activator->s.number, "print \"a line\n\"" );
}

int main( void )
{
	rpLuaPlugin_t *p;
	int fails = 0, button, relay, door, locked, npcEnt;

	// --- World.use: through GlobalUse, the player as activator and other; locked refused; NPC, held, inert skipped
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 );
	button = t_mapEntity( "func_button", "btn1", "relay1" ); relay = t_mapEntity( "target_relay", "relay1", "door1" ); door = t_mapEntity( "func_door", "door1", NULL );
	locked = t_mapEntity( "func_door", "vault", NULL ); t_locked[locked] = qtrue;
	npcEnt = t_mapEntity( "NPC", "guard", NULL ); g_entities[npcEnt].client = &g_clients_store[7];
	t_mapEntity( "misc_model", "held", NULL ); g_entities[level.num_entities - 1].rpHeldBy = 1;
	t_mapEntity( "info_null", "inert", NULL ); g_entities[level.num_entities - 1].use = NULL;
	t_write_plugin( "w", "N = 0 World.watch('btn1') World.watch('door1') Events.on('entity_used', function(e) N = N + 1 end)\n"
		"Commands.add('use', function(p, a) U, S = World.use(a[1], p) if U == false then WHY = S end end)\n"
		"Commands.add('usebob', function(p, a) U, S = World.use(a[1], Players.get(1)) end)\n" );
	t_start( "w" ); p = RP_Lua_Find( "w" ); CHECK( p && p->L, "w loaded" );
	run( p, "use btn1" ); CHECK( get_int( p, "U" ) == 1 && get_int( p, "S" ) == 0, "button used: %d used %d skipped", get_int( p, "U" ), get_int( p, "S" ) );
	CHECK( t_usedCount[button] == 1 && t_usedCount[relay] == 1 && t_usedCount[door] == 1, "the chain ran: %d %d %d", t_usedCount[button], t_usedCount[relay], t_usedCount[door] );
	CHECK( t_usedActivator[door] == &g_entities[0] && t_usedOther[button] == &g_entities[0], "the player is activator and other" );
	CHECK( get_int( p, "N" ) == 0, "no entity_used for a script's own use (and its chain): %d", get_int( p, "N" ) );
	CHECK( p->tokens == RP_LUA_TOKENS - 1 && p->uses == 1, "one token: %d", p->tokens );
	run( p, "use vault" ); CHECK( get_int( p, "U" ) == 1 && t_usedCount[locked] == 0, "a locked door: counted as used by the call, refused by the lock (%d)", t_usedCount[locked] );
	run( p, "use guard" ); CHECK( get_int( p, "U" ) == 0 && get_int( p, "S" ) == 1, "an NPC is skipped (%d/%d)", get_int( p, "U" ), get_int( p, "S" ) );
	run( p, "use held" ); CHECK( get_int( p, "S" ) == 1, "held skipped" );
	t_frame( 1000 );	// Alice's allowance: four uses a second (G1)
	refill( p ); run( p, "use inert" ); CHECK( get_int( p, "S" ) == 1, "no use: skipped (U %d S %s WHY %s)", get_int( p, "U" ), get_str( p, "S" ), get_str( p, "WHY" ) );
	refill( p ); run( p, "use nothing" ); CHECK( !strcmp( get_str( p, "WHY" ), "nothing has that targetname" ), "unknown name: %s", get_str( p, "WHY" ) );
	refill( p ); run( p, "usebob door1" ); CHECK( t_usedActivator[door] == &g_entities[1], "another player as activator" );
	// a player pressing the button fires entity_used as before
	GlobalUse( &g_entities[button], &g_entities[1], &g_entities[1] ); CHECK( get_int( p, "N" ) == 2, "a player's use fires (button and door): %d", get_int( p, "N" ) );
	// the bucket: 4 then refused (two players, so the allowance of each is not what stops it)
	t_frame( 1000 ); refill( p ); run( p, "use btn1" ); run( p, "use btn1" ); run( p, "usebob btn1" ); run( p, "usebob btn1" ); run( p, "use btn1" );
	CHECK( !strcmp( get_str( p, "WHY" ), "too fast" ), "5th use refused: %s", get_str( p, "WHY" ) );
	// G1: four uses a second to one player, all plugins together, shared with Msg.*; refused before the token
	t_frame( 1000 ); refill( p ); run( p, "use btn1" ); run( p, "use btn1" ); run( p, "use btn1" ); refill( p ); run( p, "use btn1" ); refill( p ); run( p, "use btn1" );
	CHECK( !strcmp( get_str( p, "WHY" ), "player busy" ) && p->tokens == RP_LUA_TOKENS && p->usesBusy == 1, "a fifth use to Alice in a second: %s, tokens %d", get_str( p, "WHY" ), p->tokens );
	run( p, "usebob btn1" ); CHECK( get_int( p, "U" ) == 1, "Bob still has room" );
	t_frame( 1000 ); refill( p ); run( p, "use btn1" ); CHECK( get_int( p, "U" ) == 1, "a second later Alice has room again" );
	{ int i; for ( i = 0; i < 3; i++ ) RP_LuaApi_Sent( 0 ); } refill( p ); run( p, "use btn1" ); CHECK( !strcmp( get_str( p, "WHY" ), "player busy" ), "messages to Alice share the allowance" );
	// rewards and dialogues: reached as by a button, at any depth; the reward pays the player, its log line names the plugin
	{ int chest = t_mapEntity( "func_button", "chest_btn", "chest_relay" ); int rel = t_mapEntity( "target_relay", "chest_relay", "chest_pay" ); int pay = t_rewardEntity( "chest_pay", "after_pay", 50, 1 ); int after = t_mapEntity( "target_relay", "after_pay", NULL );
	  int del = t_mapEntity( "target_delay", "late_btn", NULL ), plain = t_mapEntity( "func_button", "plain_btn", "plain_door" ); t_mapEntity( "func_door", "plain_door", NULL ); (void)rel; (void)del;
	  t_frame( 1000 ); refill( p ); t_logLen = 0; t_log[0] = 0; run( p, "use chest_btn" );
	  CHECK( get_int( p, "U" ) == 1 && t_usedCount[chest] == 1 && t_payouts == 1 && t_paidCredits[0] == 50 && t_usedCount[after] == 1, "a chain to a reward pays Alice and fires its target (%d payouts)", t_payouts );
	  CHECK( t_log_has( "Reward: chest_pay (" ) && t_log_has( "used [lua:w]: Alice" ), "the log line names the plugin: %s", t_log );
	  run( p, "use chest_pay" ); CHECK( t_payouts == 1, "ONCE_EACH: not Alice again" );
	  run( p, "usebob chest_relay" ); CHECK( t_payouts == 2 && t_paidCredits[1] == 50, "Bob, from the middle of the chain" );
	  GlobalUse( &g_entities[chest], &g_entities[2], &g_entities[2] ); CHECK( t_payouts == 2, "a player not logged in to a character: nothing (no player 2)" );
	  // a player's own use: no plugin in the log, nothing counted
	  t_player( 2, "Carol", qtrue, 9 ); t_logLen = 0; t_log[0] = 0; GlobalUse( &g_entities[chest], &g_entities[2], &g_entities[2] );
	  CHECK( t_payouts == 3 && t_log_has( "used: Carol" ) && !t_log_has( "[lua:" ), "a player's button: paid, no plugin named" );
	  run( p, "use plain_btn" ); CHECK( get_int( p, "U" ) == 1 && t_usedCount[plain] == 1 && t_usedInCall[plain] == 1, "inside a script call the flag is set (%d)", t_usedInCall[plain] );
	  GlobalUse( &g_entities[plain], &g_entities[1], &g_entities[1] ); CHECK( t_usedInCall[plain] == 0 && rp_luaInCall == 0, "a player's use: flag clear" );
	  // a loop through relays ends at the stock chain limit
	  { int a1 = t_mapEntity( "target_relay", "loopa", "loopb" ), a2 = t_mapEntity( "target_relay", "loopb", "loopa" ); (void)a1; (void)a2;
	    t_frame( 1000 ); refill( p ); run( p, "use loopa" ); CHECK( t_useRefused > 0 && t_useDepth == 0, "a loop ends at the chain limit" ); } }
	// G3: database writes from inside script calls, four a frame, all plugins together; a payout past it gives nothing, fires
	// nothing and keeps no ONCE_EACH record; the next frame pays again; a player's own use is not counted
	{ int k, after = t_mapEntity( "target_relay", "many_after", NULL ); for ( k = 0; k < 6; k++ ) t_rewardEntity( "many", "many_after", 10, 0 );
	  t_frame( 1000 ); refill( p ); t_payouts = 0; p->dbRefused = 0; t_logLen = 0; t_log[0] = 0; t_usedCount[after] = 0; run( p, "use many" );
	  CHECK( t_payouts == RP_LUA_DB_WRITES_FRAME && t_usedCount[after] == RP_LUA_DB_WRITES_FRAME && p->dbRefused == 2, "6 rewards in one call: %d paid, %d fired, %d skipped", t_payouts, t_usedCount[after], p->dbRefused );
	  CHECK( t_log_has( "[lua:w] payout skipped" ) && !strstr( strstr( t_log, "payout skipped" ) + 1, "payout skipped" ), "logged once: %s", t_log );
	  run( p, "usebob many" ); CHECK( t_payouts == RP_LUA_DB_WRITES_FRAME, "the same frame, another call: nothing (%d)", t_payouts );
	  t_frame( 50 ); refill( p ); run( p, "usebob many" ); CHECK( t_payouts == 2 * RP_LUA_DB_WRITES_FRAME, "the next frame pays again: %d", t_payouts );
	  t_payouts = 0; { gentity_t *e = NULL; while ( ( e = G_Find( e, FOFS( targetname ), "many" ) ) ) GlobalUse( e, &g_entities[2], &g_entities[2] ); }
	  CHECK( t_payouts == 6, "a player's own use: all six pay (%d)", t_payouts ); }
	{ int k, once; for ( k = 0; k < 4; k++ ) t_rewardEntity( "oncebatch", NULL, 5, 0 ); once = t_rewardEntity( "oncebatch", NULL, 5, 1 );	// the ONCE_EACH one is found last
	  t_frame( 1000 ); refill( p ); t_payouts = 0; run( p, "usebob oncebatch" );
	  CHECK( t_payouts == 4, "five in a call: four paid, the ONCE_EACH one skipped (%d)", t_payouts );
	  t_frame( 50 ); GlobalUse( &g_entities[once], &g_entities[1], &g_entities[1] ); CHECK( t_payouts == 5, "the skipped one kept no record: it pays Bob later (%d)", t_payouts ); }
	RP_LUA_SHUTDOWN();

	// --- G2: commands to a client from inside script calls, 32 a second per client, all plugins together
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 ); t_player( 2, "Bot", qtrue, 9 ); g_entities[2].r.svFlags |= SVF_BOT;
	CHECK( trap->Cvar_VariableIntegerValue( "sv_floodProtect" ) == t_floodProtect && trap->Argc() == t_argc, "the guarded table forwards the other functions unchanged" );
	{ gameImport_t *before = trap; RP_Lua_TrapGuardInstall(); CHECK( trap == before, "installed once: a second G_InitGame does not wrap again" ); }
	t_write_plugin( "g2", "Commands.add('prn', function(p, a) World.use(a[1], p) end)\n"
		"Commands.add('one', function(p, a) Msg.print(p, 'hi') end)\n" );
	t_start( "g2" ); p = RP_Lua_Find( "g2" );
	{ int k; for ( k = 0; k < 200; k++ ) t_mapEntity( "target_print", "prints", NULL ); }
	{ gentity_t *e = NULL; while ( ( e = G_Find( e, FOFS( targetname ), "prints" ) ) ) e->use = s_test_print; }
	memset( t_nsent, 0, sizeof( t_nsent ) ); t_logLen = 0; t_log[0] = 0; refill( p ); run( p, "prn prints" );
	CHECK( t_nsent[0] == RP_LUA_CMDS_PER_SEC && p->cmdsHeld == 200 - RP_LUA_CMDS_PER_SEC, "200 prints to Alice: %d sent, %d held", t_nsent[0], p->cmdsHeld );
	CHECK( t_log_has( "[lua:g2] commands to Alice^7 held back" ) && strstr( t_log, "held back" ) == strstr( t_log, "held back" ) && !strstr( strstr( t_log, "held back" ) + 1, "held back" ), "one log line: %s", t_log );
	CHECK( t_nsent[1] == 0, "Bob got none of them" );
	refill( p ); run( p, "one" ); CHECK( t_nsent[0] == RP_LUA_CMDS_PER_SEC, "Msg.print is held back too while the budget is spent" );
	memset( t_nsent, 0, sizeof( t_nsent ) ); s_test_broadcast = 1; refill( p ); run( p, "prn prints" ); s_test_broadcast = 0;
	CHECK( t_nsent[0] == 0 && t_nsent[1] == RP_LUA_CMDS_PER_SEC && t_nsent[2] > 0, "a message to everyone: none to Alice (spent), %d to Bob, the bot is not counted (%d)", t_nsent[1], t_nsent[2] );
	CHECK( t_nsent[5] == 0, "a free slot gets nothing one by one" );
	t_frame( 1000 ); memset( t_nsent, 0, sizeof( t_nsent ) ); s_test_broadcast = 1; s_test_limit = 3; s_test_count = 0; refill( p ); run( p, "prn prints" ); s_test_broadcast = 0; s_test_limit = 0;
	CHECK( t_nsent[0] == 3 && t_nsent[1] == 3 && t_nsent[5] == 3, "a second later, everyone has room: sent whole (%d %d %d)", t_nsent[0], t_nsent[1], t_nsent[5] );
	{ int k; t_nsent[0] = 0; for ( k = 0; k < 50; k++ ) trap->SendServerCommand( 0, "print \"x\n\"" ); CHECK( t_nsent[0] == 50, "outside a call: nothing counted (%d)", t_nsent[0] ); }
	CHECK( RP_Lua_ScriptCmdRoom( 0, 1000 ), "outside a call there is always room" );
	RP_LUA_SHUTDOWN();

	// --- World.prop: classes, keys, name, handle, tag, refusal text, caps, no room
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "pr", "H = {} Commands.add('prop', function(p, a) local h, why = World.prop(a[1], {x=10,y=20,z=30}, {x=0,y=90,z=0}, {model='models/map_objects/crate.md3', health=50}, {name=a[2]}) if h then H[#H+1] = h NUM = h.number CLS = h.classname TN = h.targetname OX = h.origin.x HP = h.health OK = true else OK = false WHY = why end end)\n"
		"Commands.add('bad', function(p, a) OK2, E = pcall(World.prop, a[1], {x=0,y=0,z=0}, nil, {[a[2]] = a[3]}) E = tostring(E) end)\n"
		"Commands.add('rmv', function(p, a) R = World.remove(H[tonumber(a[1])]) INUSE = H[tonumber(a[1])]:valid() end)\n"
		"Commands.add('valid', function(p, a) V = H[tonumber(a[1])]:valid() N2 = tostring(H[tonumber(a[1])].number) end)\n"
		"Commands.add('loop', function(p) for i = 1, 100 do local h, why = World.prop('misc_model_breakable', {x=0,y=0,z=0}) if not h then WHY = why return end end end)\n"
		"Commands.add('lnk', function(p, a) local h = World.prop('fx_runner', {x=0,y=0,z=0}, nil, {[a[1]] = a[2]}) LN = h and h.number or -1 end)\n"
		"Commands.add('tpset', function(p) local a, b, c, w1, w2, w3 a, w1 = World.prop('target_position', {x=500,y=0,z=0}, nil, nil, {name='there'}) b, w2 = World.prop('target_teleporter', {x=0,y=0,z=0}, nil, {target='there'}, {name='beam'}) c, w3 = World.prop('target_level_change', {x=0,y=0,z=0}, nil, {mapname='mp/ffa3'}, {name='leave'}) PN = a and a.number or -1 TN = b and b.number or -1 LN = c and c.number or -1 TWHY = tostring(w1) .. ' ' .. tostring(w2) .. ' ' .. tostring(w3) end)\n"
		"Commands.add('use', function(p, a) U, S = World.use(a[1], p) end)\n"
		"META = 0 Commands.add('raw', function(p) local o = setmetatable({x=1,y=2,z=3}, {__index = function(t, k) META = META + 1 return 0 end}) local h = World.prop('fx_runner', o, o, o, o) RN = h and h.number or -1 RX = h and h.origin.x or -1 end)\n" );
	t_start( "pr" ); p = RP_Lua_Find( "pr" );
	run( p, "prop misc_model_breakable crate" ); CHECK( get_int( p, "OK" ) == 1, "spawned: %s / %s", get_str( p, "WHY" ), strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" );
	CHECK( !strcmp( get_str( p, "CLS" ), "misc_model_breakable" ) && !strcmp( get_str( p, "TN" ), "lua.pr.crate" ) && get_int( p, "OX" ) == 10 && get_int( p, "HP" ) == 50, "handle fields: %s %s %d", get_str( p, "CLS" ), get_str( p, "TN" ), get_int( p, "HP" ) );
	{ int n = get_int( p, "NUM" ); CHECK( g_entities[n].rpLuaPlugin == (int)( p - rpLuaPlugins ) + 1 && g_entities[n].rpLuaStamp > 0, "tagged" );
	  CHECK( t_recordCount[n] == 6 && !strcmp( t_record[n][0][0], "classname" ) && !strcmp( t_record[n][3][0], "targetname" ), "record: %d pairs, %s %s", t_recordCount[n], t_record[n][0][0], t_record[n][3][0] ); }
	CHECK( p->tokens == RP_LUA_TOKENS - 1 && p->propSpawns == 1, "a token and a spawn counted" );
	run( p, "bad func_door model x" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "not a class" ), "func_door refused: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul target door.1" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "'target' is a name of one of this plugin's entities" ), "a link to a dotted name refused: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul paintarget a_very_long_name_that_cannot_fit_in_a_targetname_with_the_prefix" ); CHECK( strstr( get_str( p, "E" ), "'paintarget' is a name" ), "a link too long refused" );
	run( p, "bad misc_model_ghoul script_targetname x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "script_targetname refused" );
	run( p, "bad misc_model_ghoul npc_targetname x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "npc_targetname refused" );
	run( p, "bad misc_model_ghoul team x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "team refused" );
	run( p, "bad misc_model_ghoul rpLock x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "rpLock refused" );
	run( p, "bad misc_model_ghoul lock x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "lock (the spawn key of rpLock) refused" );
	run( p, "bad misc_model_ghoul npcxp 5" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "npcxp refused" );
	run( p, "bad misc_model_ghoul targetname x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "targetname key refused" );
	run( p, "bad misc_model_ghoul mins 1_2" ); CHECK( strstr( get_str( p, "E" ), "three numbers" ), "vector key checked: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul 9key x" ); CHECK( strstr( get_str( p, "E" ), "not a key" ), "key syntax: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul model a\"b" ); CHECK( strstr( get_str( p, "E" ), "quote" ), "quote in a value refused" );
	CHECK( p->propSpawns == 1 && p->tokens == RP_LUA_TOKENS - 1, "argument errors cost nothing" );
	t_spawnRefuse = qtrue; refill( p ); run( p, "prop fx_runner fx" ); t_spawnRefuse = qfalse;
	CHECK( get_int( p, "OK" ) == 0 && !strcmp( get_str( p, "WHY" ), "the server does not have that model" ), "the game's refusal passed through: %s", get_str( p, "WHY" ) );
	CHECK( p->spawnsRefused == 1 && RP_LuaWorld_CountLive( p, qfalse ) == 1, "refused spawn not in the list: %d live", RP_LuaWorld_CountLive( p, qfalse ) );
	t_freeSlots = 3; refill( p ); run( p, "prop weapon_blaster" ); CHECK( !strcmp( get_str( p, "WHY" ), "no room" ) && p->tokens == RP_LUA_TOKENS, "no room, no token taken" ); t_freeSlots = 1000;
	// remove: queued, gone at the frame end; a stale handle
	run( p, "rmv 1" ); CHECK( get_int( p, "R" ) == 1 && get_int( p, "INUSE" ) == 1 && g_entities[get_int( p, "NUM" )].inuse, "remove queued, still there inside the call" );
	t_frame( 50 ); CHECK( !g_entities[get_int( p, "NUM" )].inuse && t_freed == 2, "freed at the frame end (freed %d)", t_freed );
	run( p, "valid 1" ); CHECK( get_int( p, "V" ) == 0 && !strcmp( get_str( p, "N2" ), "nil" ), "stale handle: valid false, fields nil" );
	run( p, "rmv 1" ); CHECK( get_int( p, "R" ) == 0, "removing a stale handle refused" );
	// AUDIT: the slot taken again by a new prop: the old handle stays stale
	refill( p ); run( p, "prop misc_model_breakable crate2" ); CHECK( get_int( p, "NUM" ) == get_int( p, "NUM" ) && g_entities[get_int( p, "NUM" )].inuse, "a new prop" );
	{ int n2 = get_int( p, "NUM" ); refill( p ); run( p, "valid 1" ); CHECK( get_int( p, "V" ) == 0, "old handle stale after slot reuse" ); run( p, "rmv 1" ); t_frame( 50 ); CHECK( get_int( p, "R" ) == 0 && g_entities[n2].inuse, "the old handle cannot remove the new prop" ); }
	// AUDIT H3: every key that writes a target field is a link key, stored with the plugin's prefix
	{ const char *keys[] = { "npc_target2", "npc_target4", "throwtarget", "goaltarget", "target2", "NPC_TARGET2", NULL }; int k;
	  for ( k = 0; keys[k]; k++ ) { refill( p ); run( p, va( "lnk %s vault_door", keys[k] ) ); { int n = get_int( p, "LN" ); const char *v = n >= 0 ? t_recordValue( n, keys[k] ) : NULL;
	    CHECK( n >= 0 && v && !strcmp( v, "lua.pr.vault_door" ), "%s prefixed: %s", keys[k], v ? v : "(none)" ); } }
	  refill( p ); run( p, "bad fx_runner npc_target2 door.1" ); CHECK( strstr( get_str( p, "E" ), "'npc_target2' is a name" ), "a dotted npc_target2 refused" ); }
	// AUDIT M9: the option tables are read raw: an __index metamethod never runs inside a spawn
	refill( p ); run( p, "raw" ); CHECK( get_int( p, "RN" ) >= 0 && get_int( p, "RX" ) == 1 && get_int( p, "META" ) == 0, "raw reads: spawned at the raw x (%d), __index ran %d times", get_int( p, "RX" ), get_int( p, "META" ) );
	CHECK( p->errorsTotal == 0, "no errors so far: %s", strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" );
	// the quest helpers: a teleporter to the plugin's own position moves the player passed to World.use; a position has no use;
	// a level change spawns (its mapname is checked by the real spawn function), and a mapname with a separator is refused by the key filter
	{ int pos, tp, lc; g_entities[0].client->ps.origin[0] = 0;
	  refill( p ); run( p, "tpset" ); pos = get_int( p, "PN" ); tp = get_int( p, "TN" ); lc = get_int( p, "LN" );
	  CHECK( pos >= 0 && tp >= 0 && lc >= 0, "position, teleporter and level change spawned (%d %d %d): %s", pos, tp, lc, get_str( p, "TWHY" ) );
	  CHECK( pos >= 0 && !strcmp( g_entities[pos].targetname, "lua.pr.there" ) && g_entities[pos].r.currentOrigin[0] == 500 && !g_entities[pos].use, "the position: named, placed, no use" );
	  CHECK( tp >= 0 && t_recordValue( tp, "target" ) && !strcmp( t_recordValue( tp, "target" ), "lua.pr.there" ), "the teleporter's target is prefixed: %s", tp >= 0 && t_recordValue( tp, "target" ) ? t_recordValue( tp, "target" ) : "(none)" );
	  CHECK( lc >= 0 && t_recordValue( lc, "mapname" ) && !strcmp( t_recordValue( lc, "mapname" ), "mp/ffa3" ), "the level change carries its mapname" );
	  refill( p ); run( p, "use lua.pr.there" ); CHECK( get_int( p, "U" ) == 0 && get_int( p, "S" ) == 1, "a position is skipped by World.use (no use)" );
	  refill( p ); run( p, "use lua.pr.beam" ); CHECK( get_int( p, "U" ) == 1 && t_teleports == 1 && t_teleportedTo == pos && g_entities[0].client->ps.origin[0] == 500, "the teleporter moved Alice to the position (%d teleports, to %d, x %.0f)", t_teleports, t_teleportedTo, g_entities[0].client->ps.origin[0] );
	  refill( p ); run( p, "bad target_level_change mapname mp/ffa3;quit" ); CHECK( strstr( get_str( p, "E" ), "semicolon" ), "a mapname with a separator refused: %s", get_str( p, "E" ) );
	  g_entities[0].client->ps.origin[0] = 0; }
	// caps: 32 live, then 128 per map; the bucket paces the loop
	{ int i; for ( i = 0; i < 40; i++ ) { refill( p ); run( p, "loop" ); if ( !strcmp( get_str( p, "WHY" ), "too many props" ) ) break; } }
	CHECK( RP_LuaWorld_CountLive( p, qfalse ) == RP_LUA_MAX_PROPS && !strcmp( get_str( p, "WHY" ), "too many props" ), "32 live then refused: %d, %s", RP_LuaWorld_CountLive( p, qfalse ), get_str( p, "WHY" ) );
	RP_LuaWorld_PluginClose( p ); t_frame( 50 ); CHECK( RP_LuaWorld_CountLive( p, qfalse ) == 0, "all removed by the plugin close" );
	{ int i; for ( i = 0; i < 200; i++ ) { refill( p ); run( p, "loop" ); if ( !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ) ) break; if ( RP_LuaWorld_CountLive( p, qfalse ) >= 30 ) { RP_LuaWorld_PluginClose( p ); } } }
	CHECK( p->propSpawns == RP_LUA_PROP_SPAWNS_MAP && !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ), "128 spawns a map: %d, %s", p->propSpawns, get_str( p, "WHY" ) );
	// AUDIT: the name room leaves space for lua.<plugin>. with the longest plugin name
	t_write_plugin( "abcdefghijklmnopqrstuvwxyz012345", "Commands.add('nmx', function(p, a) OKN, EN = pcall(World.prop, 'misc_model_breakable', {x=0,y=0,z=0}, nil, nil, {name=('n'):rep(tonumber(a[1]))}) EN = tostring(EN) end)\n" );
	RP_Lua_QueueLoad( "abcdefghijklmnopqrstuvwxyz012345" ); t_frame( 10 ); p = RP_Lua_Find( "abcdefghijklmnopqrstuvwxyz012345" );
	if ( p && p->L ) { refill( p ); run( p, "nmx 27" ); CHECK( get_int( p, "OKN" ) == 0 && strstr( get_str( p, "EN" ), "name is" ), "a name past the room refused: %s", get_str( p, "EN" ) );
	  refill( p ); run( p, "nmx 26" ); CHECK( get_int( p, "OKN" ) == 1, "26 fits a 32-character plugin name" ); }
	else CHECK( 0, "long-named plugin loaded" );
	RP_LUA_SHUTDOWN(); CHECK( t_npcsAlive == 0, "nothing left" );

	// --- World.npc: types, caps, the spawner, removal via RP_SpawnedRemove, npc_death to the owner only
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "np", "H = {} Events.on('npc_death', function(e) DEAD = e.npc_type .. ' by ' .. (e.killer and e.killer.name or '-') end)\n"
		"Commands.add('npc', function(p, a) local h, why = World.npc(a[1], {x=1,y=2,z=3}, {x=0,y=45,z=0}, {name=a[2]}) if h then H[#H+1] = h NUM = h.number T = h.npc_type TN = h.targetname OK = true else OK = false WHY = why end end)\n"
		"Commands.add('rmv', function(p, a) R = World.remove(H[tonumber(a[1])]) end)\n"
		"Commands.add('paid', function(p, a) local h, why = World.npc('jedi', {x=0,y=0,z=0}, nil, {credits=tonumber(a[1]), xp=tonumber(a[2])}) if h then NUM = h.number C = h.credits X = h.xp OK = true else OK = false WHY = why end end)\n"
		"Commands.add('badpay', function(p, a) local v = a[2] == 'str' and '5' or tonumber(a[2]) OK2, E = pcall(World.npc, 'jedi', {x=0,y=0,z=0}, nil, {[a[1]] = v}) E = tostring(E) end)\n" );
	t_write_plugin( "other", "Events.on('npc_death', function(e) OTHER = 1 end)\n" );
	t_start( "np other" ); p = RP_Lua_Find( "np" );
	run( p, "npc jedi guard" ); CHECK( get_int( p, "OK" ) == 1 && !strcmp( get_str( p, "T" ), "jedi" ) && !strcmp( get_str( p, "TN" ), "lua.np.guard" ), "npc spawned: %s %s %s", get_str( p, "WHY" ), get_str( p, "T" ), get_str( p, "TN" ) );
	{ int n = get_int( p, "NUM" ); CHECK( g_entities[n].client && g_entities[n].rpLuaPlugin, "an NPC, tagged" ); CHECK( g_entities[n].r.currentOrigin[0] == 1 && g_entities[n].r.currentOrigin[1] == 2, "at the origin" );
	  CHECK( t_npcsAlive == 1 && p->npcSpawns == 1 && p->tokens == RP_LUA_TOKENS - 1, "counted, one token" );
	  // the spawner: a think of G_FreeEntity next frame
	  { int i, spawners = 0; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].think == G_FreeEntity && !g_entities[i].client ) spawners++; CHECK( spawners == 1, "the spawner goes next frame: %d", spawners ); }
	  t_frame( 100 ); t_runThinks(); CHECK( g_entities[n].inuse, "the NPC stays" );
	  // death: to the owner only, with the killer
	  RP_LUA_EVENT_DEATH( &g_entities[n], &g_entities[0], 3 ); CHECK( !strcmp( get_str( p, "DEAD" ), "jedi by Alice" ), "npc_death: %s", get_str( p, "DEAD" ) );
	  { rpLuaPlugin_t *o = RP_Lua_Find( "other" ); CHECK( get_int( o, "OTHER" ) == 0, "not to another plugin" ); }
	  // a map NPC's death reports nothing
	  { int m = t_mapEntity( "NPC", "mapnpc", NULL ); g_entities[m].client = &g_clients_store[6]; lua_pushstring( p->L, "x" ); lua_setglobal( p->L, "DEAD" ); RP_LUA_EVENT_DEATH( &g_entities[m], &g_entities[0], 3 ); CHECK( !strcmp( get_str( p, "DEAD" ), "x" ), "a map NPC: no event" ); g_entities[m].client = NULL; }
	  run( p, "rmv 1" ); CHECK( get_int( p, "R" ) == 1 && g_entities[n].inuse, "remove queued" ); t_frame( 50 ); CHECK( t_removedSilently == 1 && g_entities[n].think == G_FreeEntity && g_entities[n].rpLuaPlugin == 0, "RP_SpawnedRemove'd at the frame end, untagged" ); t_frame( 100 ); t_runThinks(); CHECK( !g_entities[n].inuse, "gone next frame" ); }
	// a kill reward on the plugin's own NPC: set on the spawner, copied to the NPC, read on the handle (nil for a prop)
	refill( p ); run( p, "paid 500 20" ); { int n = get_int( p, "NUM" );
	  CHECK( get_int( p, "OK" ) == 1 && get_int( p, "C" ) == 500 && get_int( p, "X" ) == 20 && g_entities[n].rpRewardCredits == 500 && g_entities[n].rpRewardXP == 20, "credits/xp on the NPC: %d %d", g_entities[n].rpRewardCredits, g_entities[n].rpRewardXP );
	  g_entities[n].rpRewardCredits = g_entities[n].rpRewardXP = 0; }	// what RP_PayNpcKillReward leaves once it paid
	refill( p ); run( p, "paid 100000 100" ); CHECK( get_int( p, "OK" ) == 1 && get_int( p, "C" ) == 100000 && get_int( p, "X" ) == 100, "the maxima" );
	refill( p ); run( p, "npc jedi" ); { int n = get_int( p, "NUM" ); CHECK( g_entities[n].rpRewardCredits == 0 && g_entities[n].rpRewardXP == 0, "no options: no reward" ); }
	refill( p ); run( p, "badpay credits 100001" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "credits is a whole number from 0 to 100000" ), "too many credits: %s", get_str( p, "E" ) );
	run( p, "badpay xp 101" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "xp is a whole number from 0 to 100" ), "too much xp: %s", get_str( p, "E" ) );
	run( p, "badpay xp -1" ); CHECK( get_int( p, "OK2" ) == 0, "negative" );
	run( p, "badpay xp 2.5" ); CHECK( get_int( p, "OK2" ) == 0, "not whole" );
	run( p, "badpay xp str" ); CHECK( get_int( p, "OK2" ) == 0, "a string" );
	CHECK( p->npcSpawns == 4, "no spawn for a bad reward: %d", p->npcSpawns );
	RP_Lua_QueueOp( "np", RPLOP_RELOAD ); t_frame( 50 ); t_frame( 100 ); t_runThinks(); p = RP_Lua_Find( "np" );
	run( p, "npc swoop" ); CHECK( get_int( p, "OK" ) == 0 && strstr( get_str( p, "WHY" ), "World.vehicle" ), "a vehicle type sent to World.vehicle: %s", get_str( p, "WHY" ) );
	run( p, "npc nosuch" ); CHECK( strstr( get_str( p, "WHY" ), "unknown" ), "unknown type refused" );
	{ int tk = p->tokens; t_freeSlots = 2; run( p, "npc jedi" ); CHECK( !strcmp( get_str( p, "WHY" ), "no room" ) && p->tokens == tk, "no room before the token" ); t_freeSlots = 1000; }
	t_npcRefuse = qtrue; run( p, "npc jedi" ); t_npcRefuse = qfalse; CHECK( !strcmp( get_str( p, "WHY" ), "no room" ) && p->spawnsRefused == 2, "NPC_Spawn_Do's refusal: %d", p->spawnsRefused );
	{ int i; for ( i = 0; i < 12; i++ ) { refill( p ); run( p, "npc stormtrooper" ); } }
	CHECK( RP_LuaWorld_CountLive( p, qtrue ) == RP_LUA_MAX_NPCS && !strcmp( get_str( p, "WHY" ), "too many npcs" ), "8 NPCs then refused: %d %s", RP_LuaWorld_CountLive( p, qtrue ), get_str( p, "WHY" ) );
	// unload takes them all, silently, after shutdown
	{ int before = t_removedSilently; RP_Lua_QueueOp( "np", RPLOP_UNLOAD ); t_frame( 50 ); CHECK( t_removedSilently - before == RP_LUA_MAX_NPCS, "unload removed every NPC: %d", t_removedSilently - before ); } t_frame( 100 ); t_runThinks(); CHECK( t_npcsAlive == 0, "all gone" );
	RP_LUA_SHUTDOWN();

	// --- the quest classes: a plugin's own dialogue, reward, flag and music; every link key a plain name of its own,
	// stored with the prefix, so an option fires only the plugin's own entities; the dialogue cap; G4 on the music
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 );
	t_write_plugin( "qp", "H = {} Commands.add('make', function(p, a) local h, why = World.prop(a[1], {x=0,y=0,z=0}, nil, KEYS, {name=a[2]}) if h then H[#H+1] = h NUM = h.number TN = h.targetname OK = true else OK = false WHY = why end end)\n"
		"Commands.add('keys', function(p, a) KEYS = {} for i = 1, #a, 2 do KEYS[a[i]] = a[i+1] end end)\n"
		"Commands.add('usex', function(p, a) U, S = World.use(a[1], p) if U == false then WHY = S end end)\n"
		"Commands.add('badk', function(p, a) OK2, E = pcall(World.prop, a[1], {x=0,y=0,z=0}, nil, {[a[2]] = a[3]}) E = tostring(E) end)\n" );
	t_start( "qp" ); p = RP_Lua_Find( "qp" );
	refill( p ); run( p, "keys credits 50 xp 2 target after spawnflags 1" ); run( p, "make target_reward pay" );
	{ int pay = get_int( p, "NUM" ); CHECK( get_int( p, "OK" ) == 1 && !strcmp( get_str( p, "TN" ), "lua.qp.pay" ) && g_entities[pay].rpRewardCredits == 50 && !strcmp( g_entities[pay].target, "lua.qp.after" ) && g_entities[pay].rpLuaPlugin, "a reward: named, its target prefixed (%s)", g_entities[pay].target ? g_entities[pay].target : "-" );
	  run( p, "keys message x" ); run( p, "make target_speaker after" ); { int after = get_int( p, "NUM" );
	    run( p, "keys flag quest_done target pay" ); run( p, "make target_flag done" ); { int fl = get_int( p, "NUM" ); CHECK( !strcmp( t_recordValue( fl, "flag" ), "quest_done" ) && !strcmp( g_entities[fl].target, "lua.qp.pay" ), "a flag: the flag name as given, the target prefixed" ); }
	    run( p, "keys music music/x.mp3" ); run( p, "make target_play_music song" ); { int mu = get_int( p, "NUM" ); CHECK( get_int( p, "OK" ) == 1 && g_entities[mu].use != (void *)1, "music spawned" ); }
	    run( p, "keys message Hi option1 Yes go1 pay option2 No go2 door1 speaker guard need1 quest_done" ); refill( p ); run( p, "make target_dialog talk" );
	    { int d = get_int( p, "NUM" ); CHECK( get_int( p, "OK" ) == 1 && !strcmp( t_recordValue( d, "go1" ), "lua.qp.pay" ) && !strcmp( t_recordValue( d, "go2" ), "lua.qp.door1" ) && !strcmp( t_recordValue( d, "speaker" ), "lua.qp.guard" ) && !strcmp( t_recordValue( d, "need1" ), "quest_done" ), "a dialogue: go1, go2 and speaker prefixed, need1 as given (%s)", t_recordValue( d, "go2" ) ); }
	    // a use of the reward through the plugin's own names: pays, then its target (the speaker) -- the plugin's, not a map one
	    t_frame( 1000 ); refill( p ); t_payouts = 0; run( p, "usex lua.qp.pay" ); CHECK( t_payouts == 1 && t_usedCount[after] == 1, "the chain stays in the plugin: paid, speaker used (%d %d)", t_payouts, t_usedCount[after] ); }
	  // a map door named door1 is not what go2 reaches
	  { int door = t_mapEntity( "func_door", "door1", NULL ); refill( p ); run( p, "usex lua.qp.door1" ); CHECK( !strcmp( get_str( p, "WHY" ), "nothing has that targetname" ) && t_usedCount[door] == 0, "the prefixed name reaches no map entity" ); } }
	run( p, "badk target_dialog go1 door.1" ); CHECK( strstr( get_str( p, "E" ), "'go1' is a name of one of this plugin's entities" ), "go<n> dotted refused" );
	run( p, "badk target_dialog speaker a-b" ); CHECK( strstr( get_str( p, "E" ), "'speaker' is a name" ), "speaker checked" );
	run( p, "badk target_flag targetname x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "targetname still opts.name only" );
	// G4: a script's use of music needs room on every client; a player's own does not
	{ int k; t_frame( 1000 ); refill( p ); t_musicChanges = 0; run( p, "usex lua.qp.song" ); CHECK( t_musicChanges == 1, "music changed once (%d)", t_musicChanges );
	  rp_luaInCall++; for ( k = 0; k < RP_LUA_CMDS_PER_SEC; k++ ) trap->SendServerCommand( 1, "print \"x\n\"" ); rp_luaInCall--;	// Bob's budget spent
	  refill( p ); run( p, "usex lua.qp.song" ); CHECK( t_musicChanges == 1 && p->cmdsHeld >= 1, "held back: one client has no room (%d)", t_musicChanges );
	  { gentity_t *m = NULL; while ( ( m = G_Find( m, FOFS( targetname ), "lua.qp.song" ) ) ) GlobalUse( m, &g_entities[1], &g_entities[1] ); } CHECK( t_musicChanges == 2, "a player's own use is not counted" );
	  t_frame( 1000 ); refill( p ); run( p, "usex lua.qp.song" ); CHECK( t_musicChanges == 3, "a second later it changes again" ); }
	// the dialogue cap: 16 a map, the other classes still spawn
	{ int k; run( p, "keys message Hi option1 Yes" ); for ( k = 0; k < 20; k++ ) { refill( p ); run( p, "make target_dialog" ); if ( !get_int( p, "OK" ) ) break; }
	  CHECK( p->dialogSpawns == RP_LUA_DIALOG_SPAWNS_MAP && !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ), "16 dialogues a map: %d, %s", p->dialogSpawns, get_str( p, "WHY" ) );
	  run( p, "keys credits 5" ); refill( p ); run( p, "make target_reward" ); CHECK( get_int( p, "OK" ) == 1, "a reward still spawns" ); }
	// the name room: lua.<plugin>.<name> fits a targetname
	run( p, "badk target_flag flag x" ); { lua_getglobal( p->L, "OK2" ); lua_pop( p->L, 1 ); }
	refill( p ); run( p, "keys flag f" ); t_args( "make target_flag a_name_of_exactly_32_characters__" ); RP_LUA_COMMAND( &g_entities[0], "make" ); CHECK( get_int( p, "OK" ) == 1 && strlen( get_str( p, "TN" ) ) < MAX_QPATH, "a 32-character name fits with a 2-letter plugin: %s", get_str( p, "TN" ) );
	RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks();

	// --- the spawn options of World.npc and World.vehicle: set on the spawner, read back on the handle; a wrong value is
	// an error with nothing spawned and no token spent; a vehicle takes team and scale only
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "op", "local function v(s) if s == 'true' then return true elseif s == 'false' then return false elseif tonumber(s) then return tonumber(s) elseif s == 'tbl' then return {} end return s end\n"
		"Commands.add('full', function(p) local h = World.npc('jedi', {x=0,y=0,z=0}, nil, {name='j', health=500, team='player', effect='ghost', frozen=true, noai=true, scale=150, hat='fedora', cape='royalcape'})\n"
		"  NUM = h.number TEAM = h.team EFF = h.effect FR = h.frozen SC = h.scale MH = h.max_health HP = h.health TN = h.targetname end)\n"
		"Commands.add('plain', function(p) local h = World.npc('jedi', {x=0,y=0,z=0}) NUM = h.number TEAM = h.team EFF = tostring(h.effect) FR = h.frozen SC = h.scale MH = h.max_health end)\n"
		"Commands.add('one', function(p, a) local h, why = World.npc('jedi', {x=0,y=0,z=0}, nil, {[a[1]] = v(a[2])}) NUM = h and h.number or -1 end)\n"
		"Commands.add('bad', function(p, a) OK2, E = pcall(World.npc, 'jedi', {x=0,y=0,z=0}, nil, {[a[1]] = v(a[2])}) E = tostring(E) end)\n"
		"Commands.add('veh', function(p, a) local h = World.vehicle('swoop', {x=0,y=0,z=0}, nil, {team='enemy', scale=120}) NUM = h.number TEAM = h.team SC = h.scale EFF = tostring(h.effect) FR = h.frozen end)\n"
		"Commands.add('vbad', function(p, a) OK2, E = pcall(World.vehicle, 'swoop', {x=0,y=0,z=0}, nil, {[a[1]] = v(a[2])}) E = tostring(E) end)\n"
		"Commands.add('propf', function(p) local h = World.prop('misc_model_breakable', {x=0,y=0,z=0}, nil, {model='models/x.md3'}) T2 = tostring(h.team) S2 = tostring(h.scale) F2 = tostring(h.frozen) end)\n" );
	t_start( "op" ); p = RP_Lua_Find( "op" );
	refill( p ); run( p, "full" ); { gentity_t *n = &g_entities[get_int( p, "NUM" )];
	  CHECK( !strcmp( get_str( p, "TEAM" ), "player" ) && !strcmp( get_str( p, "EFF" ), "ghost" ) && get_int( p, "FR" ) == 1 && get_int( p, "SC" ) == 150 && get_int( p, "MH" ) == 500 && get_int( p, "HP" ) == 500,
		"every option read back: %s %s %d %d %d %d", get_str( p, "TEAM" ), get_str( p, "EFF" ), get_int( p, "FR" ), get_int( p, "SC" ), get_int( p, "MH" ), get_int( p, "HP" ) );
	  CHECK( n->client->playerTeam == NPCTEAM_PLAYER && n->client->pers.phase_mode == RP_PHASE_GHOST && n->NPC->rpFrozen && n->NPC->behaviorState == BS_CINEMATIC && n->rpNpcScale == 150 && n->rpNpcCosmetic[RP_COSMETIC_HAT] == 1 && n->rpNpcCosmetic[RP_COSMETIC_CAPE] == 1 && ( n->spawnflags & 32 ),
		"the spawner carried them all to the NPC" );
	  CHECK( !strcmp( get_str( p, "TN" ), "lua.op.j" ), "the name keeps its prefix: %s", get_str( p, "TN" ) ); }
	refill( p ); run( p, "plain" ); { gentity_t *n = &g_entities[get_int( p, "NUM" )];
	  CHECK( !strcmp( get_str( p, "TEAM" ), "enemy" ) && !strcmp( get_str( p, "EFF" ), "nil" ) && get_int( p, "FR" ) == 0 && get_int( p, "SC" ) == 100 && get_int( p, "MH" ) == 100, "no options: the type's own (%s, %s, %d, %d, %d)", get_str( p, "TEAM" ), get_str( p, "EFF" ), get_int( p, "FR" ), get_int( p, "SC" ), get_int( p, "MH" ) );
	  CHECK( n->NPC->behaviorState != BS_CINEMATIC && !n->NPC->rpFrozen && n->client->pers.phase_mode == RP_PHASE_NONE && !n->rpNpcCosmetic[0] && !n->rpNpcCosmetic[1] && !( n->spawnflags & 32 ), "and nothing set on it" ); }
	refill( p ); run( p, "one health 1" ); CHECK( get_int( p, "NUM" ) > 0 && g_entities[get_int( p, "NUM" )].health == 1, "health 1" );
	refill( p ); run( p, "one health 100000" ); CHECK( g_entities[get_int( p, "NUM" )].health == 100000, "health 100000" );
	refill( p ); run( p, "one team NPCTEAM_NEUTRAL" ); CHECK( g_entities[get_int( p, "NUM" )].client->playerTeam == NPCTEAM_NEUTRAL, "the full team names too" );
	refill( p ); run( p, "one hat none" ); CHECK( get_int( p, "NUM" ) > 0 && g_entities[get_int( p, "NUM" )].rpNpcCosmetic[0] == 0, "hat none" );
	refill( p ); run( p, "one frozen false" ); CHECK( get_int( p, "NUM" ) > 0 && !g_entities[get_int( p, "NUM" )].NPC->rpFrozen, "frozen false" );
	{ int spawns = p->npcSpawns, tokens; struct { const char *cmd, *msg; } bad[] = {
		{ "bad health 0", "health is a whole number from 1 to 100000" }, { "bad health 100001", "health is a whole number from 1 to 100000" },
		{ "bad health 2.5", "health is a whole number" }, { "bad health str", "health is a whole number" },
		{ "bad team blue", "team is player, enemy, neutral or free" }, { "bad team 5", "team is player, enemy, neutral or free" },
		{ "bad effect hidden", "effect is holo, ghost or walkthrough" }, { "bad effect tbl", "effect is holo, ghost or walkthrough" },
		{ "bad frozen 1", "frozen is true or false" }, { "bad noai yes", "noai is true or false" },
		{ "bad scale 19", "scale is a whole number from 20 to 500" }, { "bad scale 501", "scale is a whole number from 20 to 500" },
		{ "bad hat crown2", "hat crown2 is not on the hat list" }, { "bad cape fedora", "cape fedora is not on the cape list" },
		{ "vbad health 50", "health is not for a vehicle" }, { "vbad effect ghost", "effect is not for a vehicle" }, { "vbad frozen true", "frozen is not for a vehicle" },
		{ "vbad noai true", "noai is not for a vehicle" }, { "vbad hat fedora", "hat is not for a vehicle" }, { "vbad cape royalcape", "cape is not for a vehicle" },
		{ "vbad credits 5", "credits is not for a vehicle" }, { "vbad xp 5", "xp is not for a vehicle" },
		{ "vbad team green", "team is player, enemy, neutral or free" }, { "vbad scale 600", "scale is a whole number from 20 to 500" } }; int k;
	  refill( p ); tokens = p->tokens;
	  for ( k = 0; k < (int)ARRAY_LEN( bad ); k++ ) { run( p, bad[k].cmd ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), bad[k].msg ), "%s: %s", bad[k].cmd, get_str( p, "E" ) ); }
	  CHECK( p->npcSpawns == spawns && p->vehicleSpawns == 0 && p->tokens == tokens, "nothing spawned, no token spent (%d %d %d)", p->npcSpawns, p->vehicleSpawns, p->tokens ); }
	refill( p ); run( p, "veh" ); { gentity_t *n = &g_entities[get_int( p, "NUM" )];
	  CHECK( !strcmp( get_str( p, "TEAM" ), "enemy" ) && get_int( p, "SC" ) == 120 && !strcmp( get_str( p, "EFF" ), "nil" ) && get_int( p, "FR" ) == 0 && n->m_pVehicle && n->rpNpcScale == 120, "a vehicle: team and scale (%s %d)", get_str( p, "TEAM" ), get_int( p, "SC" ) ); }
	refill( p ); run( p, "propf" ); CHECK( !strcmp( get_str( p, "T2" ), "nil" ) && !strcmp( get_str( p, "S2" ), "nil" ) && !strcmp( get_str( p, "F2" ), "nil" ), "a prop has none of them" );
	RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks();

	// --- World.vehicle: every type, the spawner, the G_VehicleSpawn tail, caps, remove refused while ridden,
	// release at a close, the sweep, the server-wide cap counting released ones
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 );
	t_write_plugin( "vh", "H = {} Commands.add('veh', function(p, a) local h, why = World.vehicle(a[1], {x=1,y=2,z=3}, {x=0,y=30,z=0}, {name=a[2]}) if h then H[#H+1] = h NUM = h.number VT = h.vehicle_type TN = h.targetname OK = true else OK = false WHY = why end end)\n"
		"Commands.add('rmv', function(p, a) R, WHY = World.remove(H[tonumber(a[1])]) R = tostring(R) end)\n"
		"Commands.add('info', function(p, a) local h = H[tonumber(a[1])] V = h:valid() PI = h.pilot and h.pilot.name or 'none' PA = tostring(h.passengers) end)\n"
		"Events.on('npc_death', function(e) DEAD = e.npc_type end)\n" );
	t_write_plugin( "vh2", "Commands.add('veh2', function(p, a) local h, why = World.vehicle(a[1], {x=0,y=0,z=0}) OK2 = h and true or false WHY2 = why end)\n" );
	t_start( "vh vh2" ); p = RP_Lua_Find( "vh" );
	{ const char *types[] = { "swoop", "xwing", "atst", "tauntaun" }; const char *vts[] = { "speeder", "fighter", "walker", "animal" }; int k;
	  for ( k = 0; k < 4; k++ ) { refill( p ); run( p, va( "veh %s v%d", types[k], k ) ); { int n = get_int( p, "NUM" );
	    CHECK( get_int( p, "OK" ) == 1 && !strcmp( get_str( p, "VT" ), vts[k] ) && !strcmp( get_str( p, "TN" ), va( "lua.vh.v%d", k ) ), "%s spawned as a %s: %s", types[k], get_str( p, "VT" ), get_str( p, "WHY" ) );
	    CHECK( !strcmp( t_lastSpawnerClass, "NPC_Vehicle" ) && g_entities[n].m_pVehicle && g_entities[n].s.angles[YAW] == 30, "spawned through an NPC_Vehicle spawner, yaw set" );
	    CHECK( g_entities[n].NPC->behaviorState == ( k == 3 ? BS_DEFAULT : BS_CINEMATIC ), "BS_CINEMATIC but for the animal (%d)", g_entities[n].NPC->behaviorState ); } } }
	CHECK( RP_LuaWorld_CountVehicles( p ) == 4 && RP_LuaWorld_CountLive( p, qtrue ) == 0, "4 vehicles, counted apart from NPCs" );
	refill( p ); run( p, "veh jedi" ); CHECK( !strcmp( get_str( p, "WHY" ), "not a vehicle type" ), "an NPC type refused: %s", get_str( p, "WHY" ) );
	run( p, "veh nosuch" ); CHECK( !strcmp( get_str( p, "WHY" ), "not a vehicle type" ), "unknown refused" );
	run( p, "veh novehfile" ); CHECK( strstr( get_str( p, "WHY" ), "spawn failed" ) && t_vehiclesAlive == 4, "no vehicle file: the spawn fails cleanly (%s)", get_str( p, "WHY" ) );
	refill( p ); run( p, "veh swoop" ); run( p, "veh swoop" ); refill( p ); run( p, "veh swoop" ); CHECK( RP_LuaWorld_CountVehicles( p ) == 6 && !strcmp( get_str( p, "WHY" ), "too many vehicles" ), "6 per plugin: %d, %s", RP_LuaWorld_CountVehicles( p ), get_str( p, "WHY" ) );
	// ridden: remove refused, info shows the rider
	{ int n1 = -1, i; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].m_pVehicle && g_entities[i].targetname && !strcmp( g_entities[i].targetname, "lua.vh.v0" ) ) n1 = i;
	  if ( n1 < 0 ) { CHECK( 0, "the swoop v0 found" ); return 1; }
	  t_ride( &g_entities[n1], &g_entities[1], 1 ); run( p, "info 1" ); CHECK( get_int( p, "V" ) == 1 && !strcmp( get_str( p, "PI" ), "Bob" ) && !strcmp( get_str( p, "PA" ), "1" ), "pilot and passengers read: %s %s", get_str( p, "PI" ), get_str( p, "PA" ) );
	  run( p, "rmv 1" ); CHECK( !strcmp( get_str( p, "R" ), "false" ) && !strcmp( get_str( p, "WHY" ), "someone is riding it" ), "remove refused while ridden: %s", get_str( p, "WHY" ) );
	  run( p, "rmv 2" ); CHECK( !strcmp( get_str( p, "R" ), "true" ), "an empty one: queued" ); { int n2 = -1; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].targetname && !strcmp( g_entities[i].targetname, "lua.vh.v1" ) ) n2 = i;
	    if ( n2 < 0 ) { CHECK( 0, "the xwing v1 found" ); return 1; }
	    t_ride( &g_entities[n2], &g_entities[0], 0 ); t_frame( 50 ); CHECK( g_entities[n2].inuse && g_entities[n2].rpLuaPlugin == (int)( p - rpLuaPlugins ) + 1 && g_entities[n2].think != G_FreeEntity, "boarded between remove and the frame end: kept" ); t_ride( &g_entities[n2], NULL, 0 ); }
	  // unload: empty ones go, the ridden one is released
	  { int before = t_removedSilently; rpLuaPlugin_t *p2 = RP_Lua_Find( "vh2" );
	    RP_Lua_QueueOp( "vh", RPLOP_RELOAD ); t_frame( 50 );
	    CHECK( t_removedSilently - before == 5 && t_freedRidden == 0, "reload: 5 empty vehicles removed silently (%d), nobody thrown out", t_removedSilently - before );
	    CHECK( g_entities[n1].inuse && g_entities[n1].rpLuaPlugin == RP_LUA_RELEASED && g_entities[n1].think != G_FreeEntity, "the ridden one released, still there" );
	    p = RP_Lua_Find( "vh" ); CHECK( p && p->L && RP_LuaWorld_CountVehicles( p ) == 0, "the reloaded plugin does not see it" );
	    RP_LUA_EVENT_DEATH( &g_entities[n1], &g_entities[0], 3 ); lua_getglobal( p->L, "DEAD" ); CHECK( lua_isnil( p->L, -1 ), "no npc_death for a released vehicle" ); lua_pop( p->L, 1 );
	    // the server-wide cap counts it: 23 more fit, across plugins
	    { int k; for ( k = 0; k < 6; k++ ) { refill( p ); run( p, "veh swoop" ); } for ( k = 0; k < 6; k++ ) { p2->tokens = RP_LUA_TOKENS; t_args( "veh2 swoop" ); RP_LUA_COMMAND( &g_entities[0], "veh2" ); } }
	    CHECK( RP_LuaWorld_CountVehicles( p ) == 6 && RP_LuaWorld_CountVehicles( p2 ) == 6, "two plugins with 6 each" );
	    t_frame( 100 ); t_runThinks();
	    // the sweep: still ridden, it stays; the rider gets off, it goes within 500 ms, through RP_SpawnedRemove, untagged
	    t_frame( 600 ); CHECK( g_entities[n1].inuse && g_entities[n1].think != G_FreeEntity, "still ridden: kept" );
	    t_ride( &g_entities[n1], NULL, 0 ); before = t_removedSilently; t_frame( 50 ); t_frame( 550 );
	    CHECK( t_removedSilently - before == 1 && g_entities[n1].rpLuaPlugin == 0 && g_entities[n1].think == G_FreeEntity, "empty now: removed by the sweep" );
	    t_frame( 100 ); t_runThinks(); CHECK( !g_entities[n1].inuse && t_freedRidden == 0, "gone, and nobody was ever thrown out" ); } }
	// the server-wide cap counts released ones; one an admin removes (or that is destroyed) stops counting at once
	RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks();
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 ); t_start( "vh vh2" ); p = RP_Lua_Find( "vh" );
	{ rpLuaPlugin_t *p2 = RP_Lua_Find( "vh2" ); int round, k, freedOne = -1;
	  for ( round = 0; round < 3; round++ )
	  { // six spawned, all ridden, the plugin reloaded: six more released
		p->vehicleSpawns = 0; for ( k = 0; k < 6; k++ ) { refill( p ); run( p, "veh swoop" ); }
		CHECK( RP_LuaWorld_CountVehicles( p ) == 6, "round %d: six spawned (%s)", round, get_str( p, "WHY" ) );
		ride_all( (int)( p - rpLuaPlugins ) + 1, &g_entities[round & 1] );
		RP_Lua_QueueOp( "vh", RPLOP_RELOAD ); t_frame( 50 ); p = RP_Lua_Find( "vh" );
	  }
	  CHECK( count_tag( RP_LUA_RELEASED ) == 18 && t_freedRidden == 0, "18 released, nobody thrown out: %d", count_tag( RP_LUA_RELEASED ) );
	  p->vehicleSpawns = 0; for ( k = 0; k < 6; k++ ) { refill( p ); run( p, "veh swoop" ); }
	  CHECK( RP_LuaWorld_CountVehicles( p ) == 6, "six more: 24 on the server" );
	  t_args( "veh2 swoop" ); RP_LUA_COMMAND( &g_entities[0], "veh2" ); lua_getglobal( p2->L, "WHY2" );
	  CHECK( !strcmp( lua_tostring( p2->L, -1 ) ? lua_tostring( p2->L, -1 ) : "", "too many vehicles on the server" ), "another plugin's 25th refused: %s", lua_tostring( p2->L, -1 ) ); lua_pop( p2->L, 1 );
	  { int i; for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].rpLuaPlugin == RP_LUA_RELEASED ) { freedOne = i; break; } }
	  t_ride( &g_entities[freedOne], NULL, 0 ); G_FreeEntity( &g_entities[freedOne] );	// /entremove of an empty one before the sweep looks
	  p2->tokens = RP_LUA_TOKENS; t_args( "veh2 swoop" ); RP_LUA_COMMAND( &g_entities[0], "veh2" ); lua_getglobal( p2->L, "OK2" );
	  CHECK( lua_toboolean( p2->L, -1 ) && RP_LuaWorld_CountVehicles( p2 ) == 1, "one gone: room for one more" ); lua_pop( p2->L, 1 );
	  t_args( "veh2 swoop" ); RP_LUA_COMMAND( &g_entities[0], "veh2" ); lua_getglobal( p2->L, "WHY2" );
	  CHECK( !strcmp( lua_tostring( p2->L, -1 ) ? lua_tostring( p2->L, -1 ) : "", "too many vehicles on the server" ), "and no more" ); lua_pop( p2->L, 1 );
	  t_frame( 600 ); t_frame( 50 ); CHECK( count_tag( RP_LUA_RELEASED ) == 17 && t_freedRidden == 0, "the sweep keeps the 17 still ridden: %d", count_tag( RP_LUA_RELEASED ) );
	  ride_all( (int)( p - rpLuaPlugins ) + 1, &g_entities[0] ); RP_Lua_QueueOp( "vh", RPLOP_RELOAD ); t_frame( 50 ); p = RP_Lua_Find( "vh" );
	  CHECK( count_tag( RP_LUA_RELEASED ) == 23, "23 released" );
	  // six of them removed by an admin, six more spawned, ridden and released before the sweep looks again:
	  // the list drops the gone ones first, so every released vehicle is tracked
	  { int i, n = 0; for ( i = MAX_CLIENTS; i < level.num_entities && n < 6; i++ ) if ( g_entities[i].inuse && g_entities[i].rpLuaPlugin == RP_LUA_RELEASED ) { t_ride( &g_entities[i], NULL, 0 ); G_FreeEntity( &g_entities[i] ); n++; } }
	  p->vehicleSpawns = 0; for ( k = 0; k < 6; k++ ) { refill( p ); run( p, "veh swoop" ); }
	  CHECK( RP_LuaWorld_CountVehicles( p ) == 6, "six spawned in their room" );
	  ride_all( (int)( p - rpLuaPlugins ) + 1, &g_entities[1] );
	  RP_Lua_QueueOp( "vh", RPLOP_RELOAD ); RP_Lua_Frame(); p = RP_Lua_Find( "vh" );
	  CHECK( count_tag( RP_LUA_RELEASED ) == 23, "23 released again" );
	  ride_all( RP_LUA_RELEASED, NULL ); t_frame( 600 ); t_frame( 50 ); t_frame( 150 ); t_runThinks();
	  CHECK( count_tag( RP_LUA_RELEASED ) == 0 && t_vehiclesAlive == 1 && t_freedRidden == 0, "everyone off: all 23 swept (%d left, %d vehicles alive)", count_tag( RP_LUA_RELEASED ), t_vehiclesAlive ); }
	// the per-map cap
	RP_LUA_SHUTDOWN(); t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_start( "vh" ); p = RP_Lua_Find( "vh" );
	{ int k; for ( k = 0; k < 40; k++ ) { refill( p ); run( p, "veh swoop" ); if ( !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ) ) break; if ( RP_LuaWorld_CountVehicles( p ) >= 6 ) { RP_LuaWorld_PluginClose( p ); t_frame( 100 ); t_runThinks(); } } }
	CHECK( p->vehicleSpawns == RP_LUA_VEHICLE_SPAWNS_MAP && !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ), "24 spawns a map: %d", p->vehicleSpawns );
	RP_LUA_SHUTDOWN(); t_frame( 100 ); t_runThinks();

	// --- Effect.play / Sound.play: lookup only, slot check, a token each
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_cs[0][0] = "sound/a.wav"; t_ncs[0] = 1; t_cs[1][0] = "env/fire"; t_cs[1][1] = "env/smoke"; t_ncs[1] = 2;
	t_write_plugin( "fx", "Commands.add('eff', function(p, a) OK, WHY = Effect.play(a[1], {x=5,y=5,z=5}, nil) WHY = tostring(WHY) end) Commands.add('snd', function(p, a) OK, WHY = Sound.play(a[1], {x=0,y=0,z=0}) WHY = tostring(WHY) end)\n"
		"Commands.add('far', function(p) OK2, E = pcall(Effect.play, 'env/fire', {x=1e9,y=0,z=0}) E = tostring(E) end)\n" );
	t_start( "fx" ); p = RP_Lua_Find( "fx" );
	run( p, "eff env/smoke" ); CHECK( get_int( p, "OK" ) == 1 && t_effects == 1 && t_lastEffect == 2 && t_lastEffectOrigin[0] == 5, "effect played by its index" );
	run( p, "eff env/rain" ); CHECK( get_int( p, "OK" ) == 0 && strstr( get_str( p, "WHY" ), "not registered" ) && t_ncs[1] == 2, "unregistered effect refused, nothing registered" );
	run( p, "snd sound/a.wav" ); CHECK( get_int( p, "OK" ) == 1 && t_sounds == 1 && t_lastSound == 1, "sound played" );
	run( p, "snd sound/b.wav" ); CHECK( get_int( p, "OK" ) == 0 && t_sounds == 1, "unregistered sound refused" );
	CHECK( p->tokens == RP_LUA_TOKENS - 2 && p->effects == 1 && p->sounds == 1, "two tokens: %d", p->tokens );
	t_freeSlots = 0; run( p, "eff env/fire" ); CHECK( !strcmp( get_str( p, "WHY" ), "no room" ), "no slot for the temp entity" ); t_freeSlots = 1000;
	run( p, "far" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "out of the map" ), "origin bounds: %s", get_str( p, "E" ) );
	run( p, "eff env/fire" ); run( p, "eff env/fire" ); run( p, "eff env/fire" ); CHECK( !strcmp( get_str( p, "WHY" ), "too fast" ), "the bucket: %s", get_str( p, "WHY" ) );
	RP_LUA_SHUTDOWN();

	printf( fails ? "%d FAILED\n" : "t_world: ALL OK\n", fails );
	return fails ? 1 : 0;
}
