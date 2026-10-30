// TEST ONLY: the real g_dialog.c (target_dialog, target_flag, /dialog) against the stubs: open, show, answer,
// the option firing with the player as activator, need<n>, ONCE_EACH, every way a dialogue closes, the menu
// protocol for a client with rpdlg, the text rules, /entflags.
#define T_REAL_DIALOG
#include "t.h"
#include <math.h>

// spawn keys for the next SP_ call: t_key() and t_keysClear() (stubs.c)
static char s_pool[1 << 16]; static int s_poolUsed;
void *G_Alloc( int size ) { void *p = &s_pool[s_poolUsed]; s_poolUsed += ( size + 31 ) & ~31; return p; }
void G_UseTargets2( gentity_t *ent, gentity_t *activator, const char *string ) { gentity_t *t = NULL; while ( ( t = G_Find( t, FOFS( targetname ), string ) ) ) GlobalUse( t, ent, activator ); }
void G_UseTargets( gentity_t *ent, gentity_t *activator ) { if ( ent->target ) G_UseTargets2( ent, activator, ent->target ); }
int ClientNumberFromString( gentity_t *to, const char *s, qboolean allowconnecting ) { (void)to; (void)allowconnecting; return atoi( s ); }
static int t_knocked[MAX_CLIENTS]; qboolean BG_InKnockDown( int anim ) { return anim == 999 ? qtrue : qfalse; }

// a target_dialog or target_flag spawned from keys, the way the game would (slot, classname, then SP_)
void SP_target_dialog( gentity_t *ent ); void SP_target_flag( gentity_t *ent );
// World.prop's target_dialog and target_flag spawned for real: the record's keys become the spawn keys
void t_spawnReal( gentity_t *ent ) { int n = ent->s.number, i; const char *keys[] = { "title", "message", "option1", "go1", "need1", "option2", "go2", "need2", "option3", "go3", "need3", "timeout", "range", "speaker", "flag", "targetname", NULL };
	t_keysClear(); for ( i = 0; keys[i]; i++ ) { const char *v = t_recordValue( n, keys[i] ); if ( v ) t_key( keys[i], v ); } ent->use = NULL;
	if ( !strcmp( ent->classname, "target_dialog" ) ) SP_target_dialog( ent ); else SP_target_flag( ent ); t_keysClear(); }
static int spawn_dialog( const char *targetname, const char *target, int spawnflags )
{ gentity_t *e = &g_entities[t_mapEntity( "target_dialog", targetname, target )]; e->spawnflags = spawnflags; e->use = NULL; t_key( "targetname", targetname ); SP_target_dialog( e ); t_keysClear(); return e->inuse ? e->s.number : -1; }
static int spawn_flag( const char *targetname, const char *flag, int spawnflags )
{ gentity_t *e = &g_entities[t_mapEntity( "target_flag", targetname, NULL )]; e->spawnflags = spawnflags; e->use = NULL; t_key( "flag", flag ); SP_target_flag( e ); t_keysClear(); return e->inuse ? e->s.number : -1; }
static const char *last_sent( int c ) { return t_nsent[c] ? t_sent[c][t_nsent[c] - 1] : ""; }
static const char *sent_with( int c, const char *word ) { int i; for ( i = t_nsent[c] - 1; i >= 0; i-- ) if ( strstr( t_sent[c][i], word ) ) return t_sent[c][i]; return ""; }
static void clear_sent( void ) { memset( t_nsent, 0, sizeof( t_nsent ) ); }
static void dialog_cmd( int c, const char *line ) { t_args( line ); Cmd_Dialog_f( &g_entities[c] ); }
static void frames( int ms ) { int i; for ( i = 0; i < ms; i += 50 ) { level.time += 50; RP_DialogFrame(); } }

int main( void )
{
	int fails = 0, d, btn, door, pay, flag, d2, f;

	// --- spawn rules
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 ); g_entities[1].client->pers.rpDialogMenu = qtrue; g_entities[0].client->pers.connectTime = 100; g_entities[1].client->pers.connectTime = 200;
	t_key( "message", "" ); t_key( "option1", "x" ); CHECK( spawn_dialog( "nomsg", NULL, 0 ) < 0 && strstr( level.rp_spawn_refusal, "message" ), "no message: refused (%s)", level.rp_spawn_refusal );
	t_key( "message", "hi" ); CHECK( spawn_dialog( "noopt", NULL, 0 ) < 0, "no option1: refused" );
	t_key( "flag", "bad name" ); CHECK( spawn_flag( "badflag", "bad name", 0 ) < 0, "a bad flag name: refused" );

	// --- a talk: title, text rules, options, the answer fires go<n> with the player as activator
	door = t_mapEntity( "func_door", "door1", NULL ); btn = t_mapEntity( "func_button", "btn1", "talk" ); (void)btn;
	t_key( "title", "@Oracle" ); t_key( "message", "Say \"hello\"\nfriend " "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" ); t_key( "option1", "Open the door" ); t_key( "go1", "door1" ); t_key( "option2", "Nothing" ); t_key( "timeout", "10" ); t_key( "range", "100" );
	d = spawn_dialog( "talk", "bye", 0 ); CHECK( d >= 0, "dialogue spawned (%s)", level.rp_spawn_refusal );
	clear_sent(); GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] );
	CHECK( level.rpDialog[0].id > 0 && level.rpDialog[0].numShown == 2, "opened for Alice: id %d, %d options", level.rpDialog[0].id, level.rpDialog[0].numShown );
	CHECK( strstr( last_sent( 0 ), "^7@Oracle" ) && strstr( last_sent( 0 ), "Say 'hello' friend" ) && strstr( last_sent( 0 ), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa aaaa" ) && strstr( last_sent( 0 ), "^31.^7 Open the door" ) && strstr( last_sent( 0 ), "/dialog <number>" ), "the console text, cleaned: %s", last_sent( 0 ) );
	CHECK( t_nsent[0] == 1 && !strstr( last_sent( 0 ), "rpdlg" ), "no menu command for a client without rpdlg" );
	dialog_cmd( 0, "dialog 5" ); CHECK( strstr( last_sent( 0 ), "no option 5" ), "a bad number: %s", last_sent( 0 ) );
	dialog_cmd( 0, "dialog" ); CHECK( strstr( last_sent( 0 ), "^31.^7 Open the door" ), "/dialog alone shows it again" );
	dialog_cmd( 0, "dialog 1" );
	CHECK( level.rpDialog[0].id == 0 && t_usedCount[door] == 1 && t_usedActivator[door] == &g_entities[0] && t_usedOther[door] == &g_entities[d], "answer 1: closed, the door used with Alice as activator (%d)", t_usedCount[door] );
	CHECK( strstr( sent_with( 0, "^3> " ), "Open the door" ) && !strstr( last_sent( 0 ), "conversation is over" ), "the choice echoed, no 'over' line after an answer" );
	CHECK( t_log_has( "chose 1" ) && t_log_has( "closed for Alice^7: answered" ), "logged" );
	dialog_cmd( 0, "dialog 1" ); CHECK( strstr( last_sent( 0 ), "No dialogue is open" ), "nothing open" );

	// --- the menu client: rpdlg and rpdlgclose, the id check, a late click says nothing
	clear_sent(); GlobalUse( &g_entities[d], &g_entities[1], &g_entities[1] );
	{ int id = level.rpDialog[1].id; char expect[128]; Com_sprintf( expect, sizeof( expect ), "rpdlg %d 10 \"^7@Oracle\" \"Say 'hello' friend ", id );
	  CHECK( t_nsent[1] == 2 && !strncmp( t_sent[1][0], expect, strlen( expect ) ) && strstr( t_sent[1][0], "\" \"Open the door\" \"Nothing\"" ) && strstr( t_sent[1][1], "print" ), "menu: rpdlg then the text: %s", t_sent[1][0] );
	  dialog_cmd( 1, va( "dialog 2 %d", id + 1 ) ); CHECK( level.rpDialog[1].id == id && t_nsent[1] == 2, "a click with another id does nothing" );
	  dialog_cmd( 1, va( "dialog 0 %d", id ) ); CHECK( level.rpDialog[1].id == 0 && !strcmp( t_sent[1][2], va( "rpdlgclose %d", id ) ) && strstr( t_sent[1][3], "conversation is over" ), "Esc: rpdlgclose, the over line" );
	  CHECK( t_usedCount[door] == 1, "nothing fired on a cancel" );
	  dialog_cmd( 1, va( "dialog 0 %d", id ) ); CHECK( t_nsent[1] == 4, "a late Esc says nothing" ); }
	// cancel fires the dialogue's own target
	{ int bye = t_mapEntity( "target_print", "bye", NULL ); GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 0" ); CHECK( t_usedCount[bye] == 1 && t_usedActivator[bye] == &g_entities[0], "walk-away fired 'target' with the player" ); }

	// --- closes by itself: timeout, range, death, knockdown, spectator, disconnect, replaced, entity gone
	clear_sent(); GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); frames( 9500 ); CHECK( level.rpDialog[0].id, "still open before the timeout" ); frames( 1000 ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": timeout" ), "timeout closes" );
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); g_entities[0].client->ps.origin[0] = 150; frames( 500 ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": away" ), "range closes" ); g_entities[0].client->ps.origin[0] = 0;
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); g_entities[0].health = 0; frames( 500 ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": died" ), "death closes" ); g_entities[0].health = 100;
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); g_entities[0].client->ps.legsAnim = 999; frames( 500 ); CHECK( level.rpDialog[0].id == 0, "knockdown closes" ); g_entities[0].client->ps.legsAnim = 0;
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); g_entities[0].client->sess.sessionTeam = TEAM_SPECTATOR; frames( 500 ); CHECK( level.rpDialog[0].id == 0, "spectator closes" ); g_entities[0].client->sess.sessionTeam = TEAM_FREE;
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); RP_DialogPlayerGone( 0, qtrue ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": left" ), "disconnect closes" );
	t_key( "message", "Other?" ); t_key( "option1", "x" ); { int d2 = spawn_dialog( "other", NULL, 0 ); GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); { int first = level.rpDialog[0].id; GlobalUse( &g_entities[d2], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].id == first + 1 && t_log_has( ": replaced" ), "another opens: replaced" ); } dialog_cmd( 0, "dialog 0" ); }
	// AUDIT H2: the same dialogue used again while open (a USE_BUTTON trigger held down) goes on: same id, nothing sent, the clock refreshed
	GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); { int first = level.rpDialog[0].id, dl = level.rpDialog[0].deadline; frames( 2000 ); clear_sent(); GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] );
	  CHECK( level.rpDialog[0].id == first && t_nsent[0] == 0 && level.rpDialog[0].deadline == dl + 2000, "used again while open: refreshed, not replaced (id %d/%d, sent %d)", level.rpDialog[0].id, first, t_nsent[0] ); }	// left open for the next check
	{ rpDialogDef_t *def = g_entities[d].rpDialogDef; g_entities[d].rpDialogDef = NULL; frames( 500 ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": gone" ), "the entity respawned: gone" ); g_entities[d].rpDialogDef = def; }
	CHECK( t_usedCount[door] == 1, "none of these fired an option" );

	// --- need<n>, target_flag, numbering of the offered options, /entflags
	pay = t_mapEntity( "target_reward", "pay", NULL ); flag = spawn_flag( "setdone", "quest_done", 0 ); f = spawn_flag( "cleardone", "quest_done", 1 );
	t_key( "message", "Report?" ); t_key( "option1", "I finished" ); t_key( "need1", "quest_done" ); t_key( "go1", "pay" ); t_key( "option2", "Not yet" ); t_key( "need2", "!quest_done" ); t_key( "option3", "Bye" );
	d2 = spawn_dialog( "quest", NULL, 0 ); CHECK( d2 >= 0, "quest dialogue" );
	clear_sent(); GlobalUse( &g_entities[d2], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].numShown == 2 && strstr( last_sent( 0 ), "^31.^7 Not yet" ) && strstr( last_sent( 0 ), "^32.^7 Bye" ) && !strstr( last_sent( 0 ), "finished" ), "without the flag: 'Not yet' is 1, 'Bye' is 2: %s", last_sent( 0 ) );
	dialog_cmd( 0, "dialog 1" ); CHECK( t_usedCount[pay] == 0, "'Not yet' pays nothing" );
	GlobalUse( &g_entities[flag], &g_entities[0], &g_entities[0] ); CHECK( RP_FlagHas( &g_entities[0], "quest_done" ) && !RP_FlagHas( &g_entities[1], "quest_done" ), "flag set on Alice only" );
	clear_sent(); GlobalUse( &g_entities[d2], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].numShown == 2 && strstr( last_sent( 0 ), "^31.^7 I finished" ) && strstr( last_sent( 0 ), "^32.^7 Bye" ), "with the flag: 'I finished' is 1" );
	dialog_cmd( 0, "dialog 1" ); CHECK( t_usedCount[pay] == 1 && t_usedActivator[pay] == &g_entities[0], "the reward used with Alice" );
	GlobalUse( &g_entities[f], &g_entities[0], &g_entities[0] ); CHECK( !RP_FlagHas( &g_entities[0], "quest_done" ), "CLEAR" );
	{ int i; for ( i = 0; i < 20; i++ ) { t_key( "flag", va( "f%d", i ) ); { int e = spawn_flag( va( "sf%d", i ), va( "f%d", i ), 0 ); GlobalUse( &g_entities[e], &g_entities[0], &g_entities[0] ); } }
	  CHECK( RP_FlagHas( &g_entities[0], "f15" ) && !RP_FlagHas( &g_entities[0], "f16" ) && t_log_has( "has 16 flags already" ), "16 flags, the 17th refused" ); }
	g_entities[0].client->pers.bitvalue = 1 << ADM_ENTITYSYSTEM; clear_sent(); t_args( "entflags 0" ); Cmd_EntFlags_f( &g_entities[0] ); CHECK( strstr( last_sent( 0 ), "holds 16 flags: f0, f1" ), "/entflags: %s", last_sent( 0 ) );

	// --- ONCE_EACH: once per connection
	t_key( "message", "Once" ); t_key( "option1", "ok" ); { int d3 = spawn_dialog( "once", NULL, 1 ); clear_sent(); GlobalUse( &g_entities[d3], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].id, "first time opens" ); dialog_cmd( 0, "dialog 1" );
	  GlobalUse( &g_entities[d3], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].id == 0, "second time: nothing" );
	  g_entities[0].client->pers.enterTime = 250; GlobalUse( &g_entities[d3], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].id == 0, "a team change (ClientBegin sets enterTime again): still nothing" );
	  g_entities[0].client->pers.connectTime = 300; GlobalUse( &g_entities[d3], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].id, "a reconnect: opens again" ); dialog_cmd( 0, "dialog 0" ); }
	// a chain: an answer opens the next dialogue
	t_key( "message", "Second" ); t_key( "option1", "end" ); { int dB = spawn_dialog( "second", NULL, 0 ); t_key( "message", "First" ); t_key( "option1", "next" ); t_key( "go1", "second" ); { int dA = spawn_dialog( "first", NULL, 0 ); GlobalUse( &g_entities[dA], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 1" ); CHECK( level.rpDialog[0].id && level.rpDialog[0].entNum == dB, "the answer opened the second dialogue" ); } }
	// bots and the longest command
	g_entities[2].inuse = qtrue; g_entities[2].client->pers.connected = CON_CONNECTED; g_entities[2].r.svFlags |= SVF_BOT; GlobalUse( &g_entities[d], &g_entities[2], &g_entities[2] ); CHECK( level.rpDialog[2].id == 0, "a bot gets no dialogue" );
	{ char big[300]; memset( big, 'W', 255 ); big[255] = 0; t_key( "title", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" ); t_key( "message", big ); { int i; for ( i = 1; i <= 6; i++ ) { t_key( va( "option%d", i ), "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" ); } }
	  { int dm = spawn_dialog( "max", NULL, 0 ); clear_sent(); GlobalUse( &g_entities[dm], &g_entities[1], &g_entities[1] ); CHECK( strlen( t_sent[1][0] ) < 1022 && strlen( t_sent[1][1] ) < 1022, "the longest dialogue fits a server command: %d and %d", (int)strlen( t_sent[1][0] ), (int)strlen( t_sent[1][1] ) ); } }

	// --- the Lua notification: dialog_answer for an answer (after the option fired) and for every other end
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 ); g_entities[0].client->pers.connectTime = 100;
	t_write_plugin( "dl", "L = {} Events.on('dialog_answer', function(e) L[#L+1] = e.dialog .. ' ' .. e.entity.number .. ' ' .. tostring(e.choice) .. ' ' .. tostring(e.option) .. ' ' .. e.reason .. ' ' .. (e.player.name or '?') end)\n" );
	t_start( "dl" ); { rpLuaPlugin_t *pl = RP_Lua_Find( "dl" ); int D2 = -1; CHECK( pl && pl->L, "plugin loaded" );
	  door = t_mapEntity( "func_door", "door2", NULL ); t_key( "message", "Open?" ); t_key( "option1", "Yes please" ); t_key( "go1", "door2" ); t_key( "option2", "No" ); t_key( "timeout", "5" ); t_key( "range", "100" );
	  d = spawn_dialog( "gate", "bye2", 0 ); { int bye2 = t_mapEntity( "target_print", "bye2", NULL ); (void)bye2; }
	  // an answer: the event comes after the door was used
	  GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 1" );
	  { lua_State *L = pl->L; lua_getglobal( L, "L" ); lua_rawgeti( L, -1, 1 ); CHECK( lua_tostring( L, -1 ) && !strcmp( lua_tostring( L, -1 ), va( "gate %d 1 Yes please answered Alice", d ) ) && t_usedCount[door] == 1, "answer event: %s", lua_tostring( L, -1 ) ); lua_pop( L, 2 ); }
	  // cancel, timeout, away, replaced, left: choice nil, the reason
	  GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 0" );
	  GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); frames( 5500 );
	  GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); g_entities[0].client->ps.origin[0] = 500; frames( 500 ); g_entities[0].client->ps.origin[0] = 0;
	  { int d2; t_key( "message", "Open?" ); t_key( "option1", "Yes please" ); t_key( "go1", "door2" ); t_key( "option2", "No" ); t_key( "timeout", "5" ); t_key( "range", "100" ); d2 = spawn_dialog( "gate2", NULL, 0 );
	    GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[d2], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 2" ); D2 = d2; }
	  GlobalUse( &g_entities[d], &g_entities[0], &g_entities[0] ); RP_DialogPlayerGone( 0, qtrue );
	  { lua_State *L = pl->L; const char *tails[] = { "nil nil cancelled Alice", "nil nil timeout Alice", "nil nil away Alice", "nil nil replaced Alice", "2 No answered Alice", "nil nil left Alice" }; char exp[6][64]; int k;
	    for ( k = 0; k < 6; k++ ) Com_sprintf( exp[k], sizeof( exp[k] ), k == 4 ? "gate2 %d %s" : "gate %d %s", k == 4 ? D2 : d, tails[k] );
	    lua_getglobal( L, "L" ); CHECK( (int)lua_rawlen( L, -1 ) == 7, "7 events: %d", (int)lua_rawlen( L, -1 ) );
	    for ( k = 0; k < 6; k++ ) { lua_rawgeti( L, -1, k + 2 ); CHECK( lua_tostring( L, -1 ) && !strcmp( lua_tostring( L, -1 ), exp[k] ), "event %d: %s (wanted %s)", k + 2, lua_tostring( L, -1 ) ? lua_tostring( L, -1 ) : "nil", exp[k] ); lua_pop( L, 1 ); } lua_pop( L, 1 ); }
	  CHECK( t_usedCount[door] == 1 && pl->errorsTotal == 0, "the door opened once, no script errors (%d)", pl->errorsTotal );
	  // a bot's use raises nothing (it gets no dialogue); the entity gone
	  g_entities[2].inuse = qtrue; g_entities[2].client->pers.connected = CON_CONNECTED; g_entities[2].r.svFlags |= SVF_BOT; GlobalUse( &g_entities[d], &g_entities[2], &g_entities[2] );
	  GlobalUse( &g_entities[d], &g_entities[1], &g_entities[1] ); { rpDialogDef_t *def = g_entities[d].rpDialogDef; g_entities[d].rpDialogDef = NULL; frames( 500 ); g_entities[d].rpDialogDef = def; }
	  { lua_State *L = pl->L; lua_getglobal( L, "L" ); CHECK( (int)lua_rawlen( L, -1 ) == 8, "the bot raised nothing, the gone one did: %d", (int)lua_rawlen( L, -1 ) ); lua_rawgeti( L, -1, 8 ); CHECK( lua_tostring( L, -1 ) && strstr( lua_tostring( L, -1 ), "gone Bob" ), "gone: %s", lua_tostring( L, -1 ) ); lua_pop( L, 2 ); }
	  RP_LUA_SHUTDOWN(); }

	// --- a script's World.use opens a dialogue; the answer is the player's own action (a reward behind it pays, no plugin
	// named); without room in the player's command budget for script calls (2) it does not open, and an open one is untouched
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); g_entities[0].client->pers.connectTime = 100;
	t_write_plugin( "ds", "Commands.add('talk', function(p, a) U, S = World.use(a[1], p) end)\n" );
	t_start( "ds" ); { rpLuaPlugin_t *pl = RP_Lua_Find( "ds" ); int dq, k, id;
	  t_rewardEntity( "dpay", NULL, 25, 0 );
	  t_key( "message", "Want it?" ); t_key( "option1", "Yes" ); t_key( "go1", "dpay" ); t_key( "timeout", "30" ); t_key( "range", "100" );
	  dq = spawn_dialog( "quest", NULL, 0 ); CHECK( dq >= 0 && pl && pl->L, "set up" );
	  t_args( "talk quest" ); RP_LUA_COMMAND( &g_entities[0], "talk" );
	  CHECK( level.rpDialog[0].id > 0 && t_payouts == 0, "a script's use opened it, nothing paid yet" );
	  t_logLen = 0; t_log[0] = 0; dialog_cmd( 0, "dialog 1" );
	  CHECK( t_payouts == 1 && t_paidCredits[0] == 25 && t_log_has( "used: Alice" ) && !t_log_has( "[lua:" ), "the answer paid as Alice's own action: %s", t_log );
	  rp_luaInCall++; for ( k = 0; k < RP_LUA_CMDS_PER_SEC - 2; k++ ) trap->SendServerCommand( 0, "print \"x\n\"" ); rp_luaInCall--;	// one was the open's
	  CHECK( RP_Lua_ScriptCmdRoom( 0, 2 ) == qtrue, "outside a call: always room" );
	  rp_luaInCall++; CHECK( RP_Lua_ScriptCmdRoom( 0, 1 ) && !RP_Lua_ScriptCmdRoom( 0, 2 ), "one command left this second" ); rp_luaInCall--;
	  t_args( "talk quest" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].id == 0, "no room for two commands: not opened" );
	  GlobalUse( &g_entities[dq], &g_entities[0], &g_entities[0] ); id = level.rpDialog[0].id; CHECK( id > 0, "Alice's own use opens it" );
	  t_args( "talk quest" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].id == id, "a script's use without room leaves the open one untouched" );
	  frames( 1000 ); t_args( "talk quest" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].id == id && level.rpDialog[0].deadline > level.time + 29000, "a second later the same dialogue is refreshed, not replaced: %d", level.rpDialog[0].id );
	  RP_LUA_SHUTDOWN(); }

	// --- a plugin's own quest: its dialogue (real g_dialog.c), reward and flag, spawned by World.prop; opened by its own
	// World.use; the answer fires the plugin's own entities as the player's action; a map name is out of reach; the
	// entity removed while open closes as "gone"
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); g_entities[0].client->pers.connectTime = 100;
	t_write_plugin( "own", "H = {} local function mk(c, k, n) local h, why = World.prop(c, {x=0,y=0,z=0}, nil, k, {name=n}) if not h then Log.info('mk ' .. c .. ': ' .. tostring(why)) end H[#H+1] = h return h end\n"
		"Events.on('init', function() mk('target_reward', {credits=30, xp=1}, 'pay') mk('target_flag', {flag='own_done', target='pay'}, 'done')\n"
		"  mk('target_dialog', {message='Again?', option1='Yes', go1='talk'}, 'again')\n"
		"  mk('target_dialog', {message='Job?', option1='I did it', need1='own_done', go1='done', option2='Open the map door', go2='door1', option3='Talk again', go3='again', timeout=30}, 'talk') end)\n"
		"Commands.add('talk', function(p) U, S = World.use('lua.own.talk', p) end)\n"
		"Commands.add('drop', function(p) World.remove(H[4]) end)\n" );
	{ int door = t_mapEntity( "func_door", "door1", NULL ); rpLuaPlugin_t *pl; int dq = -1, i;
	  t_logLen = 0; t_log[0] = 0; t_start( "own" ); pl = RP_Lua_Find( "own" ); CHECK( pl && pl->L && !strstr( t_log, "mk " ), "the four spawned: %s", t_log );
	  for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) if ( g_entities[i].inuse && g_entities[i].targetname && !strcmp( g_entities[i].targetname, "lua.own.talk" ) ) dq = i;
	  CHECK( dq >= 0 && g_entities[dq].rpDialogDef && !strcmp( g_entities[dq].rpDialogDef->go[1], "lua.own.door1" ) && !strcmp( g_entities[dq].rpDialogDef->need[0], "own_done" ), "the real dialogue: go2 prefixed, need1 as given" );
	  pl->tokens = RP_LUA_TOKENS; t_args( "talk" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].id > 0 && level.rpDialog[0].numShown == 2, "opened by the plugin's own use; 'I did it' hidden without the flag (%d shown)", level.rpDialog[0].numShown );
	  dialog_cmd( 0, "dialog 1" ); CHECK( level.rpDialog[0].id == 0 && t_usedCount[door] == 0, "'Open the map door' fires lua.own.door1: the map's door1 is untouched" );
	  pl->tokens = RP_LUA_TOKENS; t_args( "talk" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); { int id = level.rpDialog[0].id; dialog_cmd( 0, "dialog 2" ); CHECK( level.rpDialog[0].id > id && level.rpDialog[0].numShown == 1, "'Talk again' opened the plugin's other dialogue (%d)", level.rpDialog[0].id ); }
	  dialog_cmd( 0, "dialog 1" ); CHECK( level.rpDialog[0].numShown == 2, "and that one reopened the first" );
	  dialog_cmd( 0, "dialog 0" );
	  // the flag set by a map button aimed at the plugin's flag entity? no: a map cannot name lua.own.done -- the plugin uses it
	  pl->tokens = RP_LUA_TOKENS; { gentity_t *f = NULL; while ( ( f = G_Find( f, FOFS( targetname ), "lua.own.done" ) ) ) GlobalUse( f, &g_entities[0], &g_entities[0] ); }
	  CHECK( RP_FlagHas( &g_entities[0], "own_done" ) && t_payouts == 1 && t_paidCredits[0] == 30, "the flag set and its target, the reward, paid (as a player's use)" );
	  pl->tokens = RP_LUA_TOKENS; t_args( "talk" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].numShown == 3, "with the flag, 'I did it' is offered (%d)", level.rpDialog[0].numShown );
	  t_logLen = 0; t_log[0] = 0; dialog_cmd( 0, "dialog 1" ); CHECK( t_payouts == 2 && !t_log_has( "[lua:" ), "the answer paid the plugin's reward as Alice's own action" );
	  pl->tokens = RP_LUA_TOKENS; t_args( "talk" ); RP_LUA_COMMAND( &g_entities[0], "talk" ); CHECK( level.rpDialog[0].id > 0, "open again" );
	  t_args( "drop" ); RP_LUA_COMMAND( &g_entities[0], "drop" ); t_frame( 50 ); frames( 500 ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": gone" ), "removed while open: closed as gone" );
	  RP_LUA_SHUTDOWN(); }

	// --- AUDIT: a flag set twice takes one slot; a replaced dialogue sends a menu client no rpdlgclose
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); t_player( 1, "Bob", qtrue, 8 ); g_entities[1].client->pers.rpDialogMenu = qtrue;
	{ int fa = spawn_flag( "fa", "twice", 0 ), k, used = 0, da, db;
	  GlobalUse( &g_entities[fa], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[fa], &g_entities[0], &g_entities[0] );
	  for ( k = 0; k < RP_FLAGS_PER_PLAYER; k++ ) if ( g_entities[0].client->pers.rpFlags[k][0] ) used++;
	  CHECK( used == 1, "a flag set twice takes one slot (%d)", used );
	  t_key( "message", "A?" ); t_key( "option1", "x" ); da = spawn_dialog( "ra", NULL, 0 );
	  t_key( "message", "B?" ); t_key( "option1", "y" ); db = spawn_dialog( "rb", NULL, 0 );
	  GlobalUse( &g_entities[da], &g_entities[1], &g_entities[1] ); clear_sent(); GlobalUse( &g_entities[db], &g_entities[1], &g_entities[1] );
	  CHECK( !sent_with( 1, "rpdlgclose" )[0] && sent_with( 1, "rpdlg " )[0], "replaced: the new rpdlg, no rpdlgclose" ); }

	// --- AUDIT H1: a use that opens nothing (ONCE_EACH already seen) leaves the open dialogue's option map as it is
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); g_entities[0].client->pers.connectTime = 100;
	{ int da, db, gated = t_mapEntity( "func_door", "gated", NULL ), plain = t_mapEntity( "func_door", "plain", NULL );
	  t_key( "message", "A?" ); t_key( "option1", "VIP only" ); t_key( "need1", "vip" ); t_key( "go1", "gated" ); t_key( "option2", "Bye" ); t_key( "go2", "plain" ); da = spawn_dialog( "pa", NULL, 0 );
	  t_key( "message", "B?" ); t_key( "option1", "x" ); t_key( "option2", "y" ); db = spawn_dialog( "pb", NULL, 1 );
	  GlobalUse( &g_entities[db], &g_entities[0], &g_entities[0] ); dialog_cmd( 0, "dialog 0" );	// B seen once
	  GlobalUse( &g_entities[da], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].numShown == 1 && level.rpDialog[0].shown[0] == 1, "A shows only Bye, as 1" );
	  GlobalUse( &g_entities[db], &g_entities[0], &g_entities[0] );	// refused: already seen
	  CHECK( level.rpDialog[0].numShown == 1 && level.rpDialog[0].shown[0] == 1 && level.rpDialog[0].entNum == da, "the refused open left A's map alone" );
	  dialog_cmd( 0, "dialog 1" ); CHECK( t_usedCount[gated] == 0 && t_usedCount[plain] == 1, "'1' fires the option shown (Bye): gated %d, plain %d", t_usedCount[gated], t_usedCount[plain] );
	  // nothing to say either: a dialogue whose every option needs a flag the player lacks
	  t_key( "message", "C?" ); t_key( "option1", "secret" ); t_key( "need1", "vip" ); { int dc = spawn_dialog( "pcx", NULL, 0 );
	    GlobalUse( &g_entities[da], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[dc], &g_entities[0], &g_entities[0] );
	    CHECK( level.rpDialog[0].entNum == da && level.rpDialog[0].numShown == 1 && level.rpDialog[0].shown[0] == 1, "nothing to say: A's map untouched" ); dialog_cmd( 0, "dialog 0" ); } }

	// --- AUDIT H1/M1: a dialog_answer "replaced" handler that opens another dialogue for the same player; and a script's own
	// World.use replacing a dialogue raises nothing (an event inside a script call is not delivered, and logged so)
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); g_entities[0].client->pers.connectTime = 100;
	t_write_plugin( "rp", "L = {} Events.on('dialog_answer', function(e) L[#L+1] = e.reason if e.reason == 'replaced' then World.use('pc', e.player) end end)\nCommands.add('talk', function(p, a) U, S = World.use(a[1], p) end)\n" );
	t_start( "rp" ); { rpLuaPlugin_t *pl = RP_Lua_Find( "rp" ); int da, db, dc;
	  t_key( "message", "A?" ); t_key( "option1", "a1" ); da = spawn_dialog( "pa", NULL, 0 );
	  t_key( "message", "B?" ); t_key( "option1", "b1" ); t_key( "option2", "b2" ); t_key( "option3", "b3" ); db = spawn_dialog( "pb", NULL, 0 );
	  t_key( "message", "C?" ); t_key( "option1", "c1" ); t_key( "option2", "c2" ); dc = spawn_dialog( "pc", NULL, 0 ); (void)dc;
	  GlobalUse( &g_entities[da], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[db], &g_entities[0], &g_entities[0] );	// the player's own: the handler opens C in the middle
	  CHECK( level.rpDialog[0].entNum == db && level.rpDialog[0].def == g_entities[db].rpDialogDef && level.rpDialog[0].numShown == 3 && level.rpDialog[0].shown[2] == 2, "the record is B's, whole (entity %d, %d shown)", level.rpDialog[0].entNum, level.rpDialog[0].numShown );
	  CHECK( pl && pl->errorsTotal == 0 && t_log_has( "(pc, entity" ), "the handler's open happened and was then replaced (no errors)" );
	  t_logLen = 0; t_log[0] = 0; pl->tokens = RP_LUA_TOKENS; t_args( "talk pa" ); RP_LUA_COMMAND( &g_entities[0], "talk" );
	  CHECK( level.rpDialog[0].entNum == da && t_log_has( "dialog_answer event raised inside a script call: not delivered" ) && !t_log_has( "nested script call refused" ), "a script's replacement: no event, logged as not delivered, not as a C bug" );
	  RP_LUA_SHUTDOWN(); }

	// --- AUDIT M1: a need-gated option is refused at answer time when the flag went away while the dialogue was open
	t_init( 8 ); t_player( 0, "Alice", qtrue, 7 ); g_entities[0].client->pers.connectTime = 100;
	{ int dg, fset, fclr, gated = t_mapEntity( "func_door", "gdoor", NULL ), plain = t_mapEntity( "func_door", "pdoor", NULL );
	  fset = spawn_flag( "fset", "key", 0 ); fclr = spawn_flag( "fclr", "key", 1 );
	  t_key( "message", "G?" ); t_key( "option1", "Use the key" ); t_key( "need1", "key" ); t_key( "go1", "gdoor" ); t_key( "option2", "Chat" ); t_key( "go2", "pdoor" ); dg = spawn_dialog( "gd", NULL, 0 );
	  GlobalUse( &g_entities[fset], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[dg], &g_entities[0], &g_entities[0] ); CHECK( level.rpDialog[0].numShown == 2, "with the flag both options are offered" );
	  GlobalUse( &g_entities[fclr], &g_entities[0], &g_entities[0] ); CHECK( !RP_FlagHas( &g_entities[0], "key" ), "the flag cleared while open" );
	  { int id = level.rpDialog[0].id; clear_sent(); dialog_cmd( 0, "dialog 1" );
	    CHECK( t_usedCount[gated] == 0 && strstr( sent_with( 0, "no longer" ), "no longer open" ) && level.rpDialog[0].id > id && level.rpDialog[0].numShown == 1 && level.rpDialog[0].shown[0] == 1, "'1' refused, the dialogue reopened with the options held now (%d shown)", level.rpDialog[0].numShown ); }
	  dialog_cmd( 0, "dialog 1" ); CHECK( t_usedCount[plain] == 1 && t_usedCount[gated] == 0, "'1' is now Chat" );
	  // every option gated and gone: it closes
	  t_key( "message", "V?" ); t_key( "option1", "VIP" ); t_key( "need1", "key" ); { int dv = spawn_dialog( "vd", NULL, 0 );
	    GlobalUse( &g_entities[fset], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[dv], &g_entities[0], &g_entities[0] ); GlobalUse( &g_entities[fclr], &g_entities[0], &g_entities[0] );
	    dialog_cmd( 0, "dialog 1" ); CHECK( level.rpDialog[0].id == 0 && t_log_has( ": gone" ), "nothing left to say: closed" ); } }

	t_init( 8 );	// TEST ONLY: frees what the stubs still hold (spawned strings, NPC clients): valgrind ends with nothing reachable
	printf( fails ? "%d FAILED\n" : "t_dialog: ALL OK\n", fails );
	return fails ? 1 : 0;
}
