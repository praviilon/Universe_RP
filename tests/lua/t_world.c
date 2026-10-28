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
	refill( p ); run( p, "use inert" ); CHECK( get_int( p, "S" ) == 1, "no use: skipped (U %d S %s WHY %s)", get_int( p, "U" ), get_str( p, "S" ), get_str( p, "WHY" ) );
	refill( p ); run( p, "use nothing" ); CHECK( !strcmp( get_str( p, "WHY" ), "nothing has that targetname" ), "unknown name: %s", get_str( p, "WHY" ) );
	refill( p ); run( p, "usebob door1" ); CHECK( t_usedActivator[door] == &g_entities[1], "another player as activator" );
	// a player pressing the button fires entity_used as before
	GlobalUse( &g_entities[button], &g_entities[1], &g_entities[1] ); CHECK( get_int( p, "N" ) == 2, "a player's use fires (button and door): %d", get_int( p, "N" ) );
	// the bucket: 4 then refused
	refill( p ); run( p, "use btn1" ); run( p, "use btn1" ); run( p, "use btn1" ); run( p, "use btn1" ); run( p, "use btn1" );
	CHECK( !strcmp( get_str( p, "WHY" ), "too fast" ), "5th use refused: %s", get_str( p, "WHY" ) );
	// rewards: a chain that reaches a target_reward is refused up front, however deep; the in-call flag is set
	// for every use a script makes (target_reward_use refuses on it) and clear for a player's
	{ int chest = t_mapEntity( "func_button", "chest_btn", "chest_relay" ); int rel = t_mapEntity( "target_relay", "chest_relay", "chest_delay" ); int del = t_mapEntity( "target_delay", "chest_delay", "chest_pay" ); int pay = t_mapEntity( "target_reward", "chest_pay", NULL );
	  int plain = t_mapEntity( "func_button", "plain_btn", "plain_door" ); t_mapEntity( "func_door", "plain_door", NULL ); (void)rel; (void)del;
	  refill( p ); run( p, "use chest_btn" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a reward" ) && t_usedCount[chest] == 0 && t_usedCount[pay] == 0 && p->tokens == RP_LUA_TOKENS, "a chain to a reward refused before the token: %s", get_str( p, "WHY" ) );
	  run( p, "use chest_relay" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a reward" ), "from the middle of the chain too" );
	  run( p, "use chest_pay" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a reward" ), "the reward itself" );
	  g_entities[del].target = "chest_relay"; run( p, "use chest_delay" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a reward" ) || t_usedCount[pay] == 0, "a loop in the chain ends (%s)", get_str( p, "WHY" ) ); g_entities[del].target = "chest_pay";
	  run( p, "use plain_btn" ); CHECK( get_int( p, "U" ) == 1 && t_usedCount[plain] == 1 && t_usedInCall[plain] == 1, "an unrelated chain still works, and inside a script call the flag is set (%d)", t_usedInCall[plain] );
	  GlobalUse( &g_entities[plain], &g_entities[1], &g_entities[1] ); CHECK( t_usedInCall[plain] == 0 && rp_luaInCall == 0, "a player's use: flag clear" );
	  // dialogues: a chain reaching a target_dialog is refused; a reward behind a dialogue's option is seen through the dialogue
	  { int tb = t_mapEntity( "func_button", "talk_btn", "talk_dlg" ); int dlg = t_mapEntity( "target_dialog", "talk_dlg", NULL ); int other = t_mapEntity( "func_button", "quiz_btn", "quiz_relay" ); int qr = t_mapEntity( "target_relay", "quiz_relay", "quiz_dlg" ); int qd = t_mapEntity( "target_dialog", "quiz_dlg", NULL ); (void)qr; (void)qd;
	    t_dialogGo[qd][1] = "chest_pay"; (void)other;
	    refill( p ); run( p, "use talk_btn" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a dialogue" ) && t_usedCount[tb] == 0 && t_usedCount[dlg] == 0 && p->tokens == RP_LUA_TOKENS, "a dialogue refused: %s", get_str( p, "WHY" ) );
	    run( p, "use quiz_btn" ); CHECK( !strcmp( get_str( p, "WHY" ), "leads to a dialogue" ), "a dialogue behind a relay refused: %s", get_str( p, "WHY" ) );
	    CHECK( t_usedCount[pay] == 0, "the reward behind the quiz was never reached" ); } }
	RP_LUA_SHUTDOWN();

	// --- World.prop: classes, keys, name, handle, tag, refusal text, caps, no room
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "pr", "H = {} Commands.add('prop', function(p, a) local h, why = World.prop(a[1], {x=10,y=20,z=30}, {x=0,y=90,z=0}, {model='models/map_objects/crate.md3', health=50}, {name=a[2]}) if h then H[#H+1] = h NUM = h.number CLS = h.classname TN = h.targetname OX = h.origin.x HP = h.health OK = true else OK = false WHY = why end end)\n"
		"Commands.add('bad', function(p, a) OK2, E = pcall(World.prop, a[1], {x=0,y=0,z=0}, nil, {[a[2]] = a[3]}) E = tostring(E) end)\n"
		"Commands.add('rmv', function(p, a) R = World.remove(H[tonumber(a[1])]) INUSE = H[tonumber(a[1])]:valid() end)\n"
		"Commands.add('valid', function(p, a) V = H[tonumber(a[1])]:valid() N2 = tostring(H[tonumber(a[1])].number) end)\n"
		"Commands.add('loop', function(p) for i = 1, 100 do local h, why = World.prop('misc_model_breakable', {x=0,y=0,z=0}) if not h then WHY = why return end end end)\n" );
	t_start( "pr" ); p = RP_Lua_Find( "pr" );
	run( p, "prop misc_model_breakable crate" ); CHECK( get_int( p, "OK" ) == 1, "spawned: %s / %s", get_str( p, "WHY" ), strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" );
	CHECK( !strcmp( get_str( p, "CLS" ), "misc_model_breakable" ) && !strcmp( get_str( p, "TN" ), "lua.pr.crate" ) && get_int( p, "OX" ) == 10 && get_int( p, "HP" ) == 50, "handle fields: %s %s %d", get_str( p, "CLS" ), get_str( p, "TN" ), get_int( p, "HP" ) );
	{ int n = get_int( p, "NUM" ); CHECK( g_entities[n].rpLuaPlugin == (int)( p - rpLuaPlugins ) + 1 && g_entities[n].rpLuaStamp > 0, "tagged" );
	  CHECK( t_recordCount[n] == 6 && !strcmp( t_record[n][0][0], "classname" ) && !strcmp( t_record[n][3][0], "targetname" ), "record: %d pairs, %s %s", t_recordCount[n], t_record[n][0][0], t_record[n][3][0] ); }
	CHECK( p->tokens == RP_LUA_TOKENS - 1 && p->propSpawns == 1, "a token and a spawn counted" );
	run( p, "bad func_door model x" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "not a class" ), "func_door refused: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul target door1" ); CHECK( get_int( p, "OK2" ) == 0 && strstr( get_str( p, "E" ), "not allowed" ), "target refused: %s", get_str( p, "E" ) );
	run( p, "bad misc_model_ghoul rpLock x" ); CHECK( strstr( get_str( p, "E" ), "not allowed" ), "rpLock refused" );
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
	// caps: 32 live, then 128 per map; the bucket paces the loop
	{ int i; for ( i = 0; i < 40; i++ ) { refill( p ); run( p, "loop" ); if ( !strcmp( get_str( p, "WHY" ), "too many props" ) ) break; } }
	CHECK( RP_LuaWorld_CountLive( p, qfalse ) == RP_LUA_MAX_PROPS && !strcmp( get_str( p, "WHY" ), "too many props" ), "32 live then refused: %d, %s", RP_LuaWorld_CountLive( p, qfalse ), get_str( p, "WHY" ) );
	RP_LuaWorld_PluginClose( p ); t_frame( 50 ); CHECK( RP_LuaWorld_CountLive( p, qfalse ) == 0, "all removed by the plugin close" );
	{ int i; for ( i = 0; i < 200; i++ ) { refill( p ); run( p, "loop" ); if ( !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ) ) break; if ( RP_LuaWorld_CountLive( p, qfalse ) >= 30 ) { RP_LuaWorld_PluginClose( p ); } } }
	CHECK( p->propSpawns == RP_LUA_PROP_SPAWNS_MAP && !strcmp( get_str( p, "WHY" ), "spawn limit for this map" ), "128 spawns a map: %d, %s", p->propSpawns, get_str( p, "WHY" ) );
	RP_LUA_SHUTDOWN(); CHECK( t_npcsAlive == 0, "nothing left" );

	// --- World.npc: types, caps, the spawner, removal via RP_SpawnedRemove, npc_death to the owner only
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 );
	t_write_plugin( "np", "H = {} Events.on('npc_death', function(e) DEAD = e.npc_type .. ' by ' .. (e.killer and e.killer.name or '-') end)\n"
		"Commands.add('npc', function(p, a) local h, why = World.npc(a[1], {x=1,y=2,z=3}, {x=0,y=45,z=0}, {name=a[2]}) if h then H[#H+1] = h NUM = h.number T = h.npc_type TN = h.targetname OK = true else OK = false WHY = why end end)\n"
		"Commands.add('rmv', function(p, a) R = World.remove(H[tonumber(a[1])]) end)\n" );
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
	run( p, "npc swoop" ); CHECK( get_int( p, "OK" ) == 0 && strstr( get_str( p, "WHY" ), "World.vehicle" ), "a vehicle type sent to World.vehicle: %s", get_str( p, "WHY" ) );
	run( p, "npc nosuch" ); CHECK( strstr( get_str( p, "WHY" ), "unknown" ), "unknown type refused" );
	t_freeSlots = 2; run( p, "npc jedi" ); CHECK( !strcmp( get_str( p, "WHY" ), "no room" ) && p->tokens == RP_LUA_TOKENS - 1, "no room before the token" ); t_freeSlots = 1000;
	t_npcRefuse = qtrue; run( p, "npc jedi" ); t_npcRefuse = qfalse; CHECK( !strcmp( get_str( p, "WHY" ), "no room" ) && p->spawnsRefused == 2, "NPC_Spawn_Do's refusal: %d", p->spawnsRefused );
	{ int i; for ( i = 0; i < 12; i++ ) { refill( p ); run( p, "npc stormtrooper" ); } }
	CHECK( RP_LuaWorld_CountLive( p, qtrue ) == RP_LUA_MAX_NPCS && !strcmp( get_str( p, "WHY" ), "too many npcs" ), "8 NPCs then refused: %d %s", RP_LuaWorld_CountLive( p, qtrue ), get_str( p, "WHY" ) );
	// unload takes them all, silently, after shutdown
	RP_Lua_QueueOp( "np", RPLOP_UNLOAD ); t_frame( 50 ); CHECK( t_removedSilently == 1 + RP_LUA_MAX_NPCS, "unload removed every NPC: %d", t_removedSilently ); t_frame( 100 ); t_runThinks(); CHECK( t_npcsAlive == 0, "all gone" );
	RP_LUA_SHUTDOWN();

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
