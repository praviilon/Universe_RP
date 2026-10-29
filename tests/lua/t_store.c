// TEST ONLY: storage: round trips, limits, slices, the orphan sweep and the character cleanup
#include "t.h"
#include "sqlite/sqlite3.h"

static const char *get_str( rpLuaPlugin_t *p, const char *name ) { static char b[8192]; lua_getglobal( p->L, name ); Q_strncpyz( b, lua_tostring( p->L, -1 ) ? lua_tostring( p->L, -1 ) : "(nil)", sizeof( b ) ); lua_pop( p->L, 1 ); return b; }
static int count_rows( const char *sql ) { sqlite3 *db; sqlite3_stmt *st; int n = -1; if ( sqlite3_open( "./home/" RP_MOD_FOLDER "/database/lua.db", &db ) != SQLITE_OK ) return -1; if ( sqlite3_prepare_v2( db, sql, -1, &st, NULL ) == SQLITE_OK ) { if ( sqlite3_step( st ) == SQLITE_ROW ) n = sqlite3_column_int( st, 0 ); sqlite3_finalize( st ); } sqlite3_close( db ); return n; }

int main( void )
{
	rpLuaPlugin_t *p;
	int i;
	t_fresh(); t_init( 8 ); t_player( 0, "Alice", qtrue, 11 ); t_player( 1, "Bob", qfalse, 0 );
	// an accounts.db with Characters 11 and 12, for the sweep
	t_mkdirs( "./home/" RP_MOD_FOLDER "/database" );
	{ sqlite3 *db; sqlite3_open( "./home/" RP_MOD_FOLDER "/database/accounts.db", &db ); sqlite3_exec( db, "CREATE TABLE Characters (CharID INTEGER PRIMARY KEY, Name TEXT); INSERT INTO Characters VALUES (11, 'a'), (12, 'b');", 0, 0, NULL ); sqlite3_close( db ); }

	t_write_plugin( "s", "Commands.add('write', function(p, a) Storage.set('k1', 'v1') Storage.set('n', 42) Storage.char.set(p, 'visits', 3) R1 = Storage.get('k1') R2 = Storage.get('n') R3 = Storage.char.get(p, 'visits') R4 = tostring(Storage.get('none')) Storage.del('k1') R5 = tostring(Storage.get('k1')) end)\n"
		"N = 0 Commands.add('big', function(p) OK1 = tostring(pcall(Storage.set, 'b', ('x'):rep(4097))) for i = 1, 5 do local ok, why = Storage.set('big' .. (N + 1), ('x'):rep(4000)) if ok then N = N + 1 else WHY = tostring(why) local ok2, why2 = Storage.set('tiny', ('y'):rep(4000)) WHY2 = tostring(why2) return end end end)\n"
		"Commands.add('nochar', function(p) OK2 = tostring(pcall(Storage.char.get, Players.get(1), 'x')) end)\n" );
	t_start( "s" ); p = RP_Lua_Find( "s" ); CHECK( p && p->L, "s loaded" );
	t_args( "write" ); RP_LUA_COMMAND( &g_entities[0], "write" );
	CHECK( !strcmp( get_str( p, "R1" ), "v1" ) && !strcmp( get_str( p, "R2" ), "42" ) && !strcmp( get_str( p, "R3" ), "3" ) && !strcmp( get_str( p, "R4" ), "nil" ) && !strcmp( get_str( p, "R5" ), "nil" ), "round trip" );
	CHECK( count_rows( "SELECT COUNT(*) FROM plugin_kv" ) == 0, "nothing flushed yet" );
	for ( i = 0; i < 31; i++ ) t_frame( 1000 );
	CHECK( count_rows( "SELECT COUNT(*) FROM plugin_kv WHERE plugin = 's'" ) == 1 && count_rows( "SELECT COUNT(*) FROM char_kv WHERE char_id = 11" ) == 1, "flushed after 30 s: %d %d", count_rows( "SELECT COUNT(*) FROM plugin_kv" ), count_rows( "SELECT COUNT(*) FROM char_kv" ) );
	// limits: 4097 raises; 1 MB fills at about 250 rows; a flush goes in slices of 64 a frame
	for ( i = 0; i < 60; i++ ) { t_args( "big" ); RP_LUA_COMMAND( &g_entities[0], "big" ); t_frame( 50 ); }	// 5 rows a call: under the 10 ms even under ASan
	CHECK( !strcmp( get_str( p, "OK1" ), "false" ), "4097 raises (OK1 %s)", get_str( p, "OK1" ) ); CHECK( p->errorsTotal == 0, "no error filling: %d: %s", p->errorsTotal, strstr( t_log, "error" ) ? strstr( t_log, "error" ) : "-" );
	{ int n; lua_getglobal( p->L, "N" ); n = (int)lua_tointeger( p->L, -1 ); lua_pop( p->L, 1 ); CHECK( n >= 240 && n <= 260, "filled at %d rows", n ); CHECK( !strcmp( get_str( p, "WHY" ), "storage full" ), "why: %s", get_str( p, "WHY" ) ); CHECK( !strcmp( get_str( p, "WHY2" ), "storage full" ), "a 4000-byte one is refused too: %s", get_str( p, "WHY2" ) ); CHECK( p->storeBytes <= RP_LUA_STORE_MAX, "bytes %d", (int)p->storeBytes ); }
	CHECK( p->numPending >= 240, "pending before the flush: %d", p->numPending );
	for ( i = 0; i < 31 && p->flushCursor < 0; i++ ) t_frame( 1000 );	// the first slice of the flush
	CHECK( count_rows( "SELECT COUNT(*) FROM plugin_kv" ) < 240 && p->flushCursor >= 0 && p->flushCursor % 64 == 0, "first slices only: %d rows, cursor %d", count_rows( "SELECT COUNT(*) FROM plugin_kv" ), p->flushCursor );
	for ( i = 0; i < 6; i++ ) t_frame( 50 ); CHECK( p->flushCursor < 0 && p->numPending == 0 && count_rows( "SELECT COUNT(*) FROM plugin_kv" ) >= 240, "flush done in slices: %d rows", count_rows( "SELECT COUNT(*) FROM plugin_kv" ) );
	// a player without a character
	t_args( "nochar" ); RP_LUA_COMMAND( &g_entities[0], "nochar" ); CHECK( !strcmp( get_str( p, "OK2" ), "false" ), "no character raises" );
	// character removed: rows go
	{ sqlite3 *db; sqlite3_open( "./home/" RP_MOD_FOLDER "/database/accounts.db", &db ); sqlite3_exec( db, "INSERT INTO Characters VALUES (13, 'c')", 0, 0, NULL ); sqlite3_close( db ); }
	t_args( "lua Storage.char.set(Players.get(0), 'q', 'z')" ); Svcmd_Lua_f(); // rcon writes under plugin "rcon", flushed at close
	CHECK( count_rows( "SELECT COUNT(*) FROM char_kv WHERE plugin = 'rcon'" ) == 1, "rcon storage flushed at close" );
	RP_LUA_CHAR_REMOVED( 11 ); CHECK( count_rows( "SELECT COUNT(*) FROM char_kv WHERE char_id = 11" ) == 0, "character 11 rows deleted" );
	// orphans swept at init, the plugin's byte count read back
	{ sqlite3 *db; sqlite3_open( "./home/" RP_MOD_FOLDER "/database/lua.db", &db ); sqlite3_exec( db, "INSERT INTO char_kv VALUES ('s', 99, 'k', 'v'), ('s', 12, 'k', 'v')", 0, 0, NULL ); sqlite3_close( db ); }
	RP_LUA_SHUTDOWN(); t_init( 8 ); t_player( 0, "Alice", qtrue, 11 ); t_start( "s" ); p = RP_Lua_Find( "s" );
	CHECK( count_rows( "SELECT COUNT(*) FROM char_kv WHERE char_id = 99" ) == 0 && count_rows( "SELECT COUNT(*) FROM char_kv WHERE char_id = 12" ) == 1, "orphan 99 swept, 12 kept" );
	CHECK( p->storeBytes > 900 * 1024, "bytes read back: %d", (int)p->storeBytes );
	RP_LUA_SHUTDOWN();
	// --- AUDIT M5: bytes are counted as bytes after a reload too (LENGTH of a TEXT counts characters, and stops at a NUL)
	t_init( 8 ); t_player( 0, "Alice", qtrue, 11 );
	t_write_plugin( "mb", "Commands.add('mbset', function(p) Storage.set('u', '\\208\\159\\209\\128\\208\\184') Storage.set('z', 'a\\0bc') end) Commands.add('mbd', function(p) OKD = Storage.del('u') OKZ = Storage.del('z') OKS, WHYS = Storage.set('u', 'x') WHYS = tostring(WHYS) end)\n" );
	t_start( "mb" ); p = RP_Lua_Find( "mb" ); t_args( "mbset" ); RP_LUA_COMMAND( &g_entities[0], "mbset" );
	CHECK( p->storeBytes == ( 1 + 6 + 32 ) + ( 1 + 4 + 32 ), "pending: %d bytes", (int)p->storeBytes );
	RP_LUA_SHUTDOWN(); t_init( 8 ); t_player( 0, "Alice", qtrue, 11 ); t_start( "mb" ); p = RP_Lua_Find( "mb" );
	CHECK( p->storeBytes == ( 1 + 6 + 32 ) + ( 1 + 4 + 32 ), "read back from the database: %d bytes", (int)p->storeBytes );
	p->storeBytes = 0;	// as if the database had counted less than the rows hold: a delete must not underflow the count
	t_args( "mbd" ); RP_LUA_COMMAND( &g_entities[0], "mbd" );
	CHECK( p->storeBytes == 1 + 1 + 32 && !strcmp( get_str( p, "WHYS" ), "nil" ), "deleted then set again, from a short count: %d bytes, %s", (int)p->storeBytes, get_str( p, "WHYS" ) );
	RP_LUA_SHUTDOWN();

	// --- AUDIT M5: a key written by a slice flush and then set again has two pending entries; the character removed subtracts once
	t_init( 8 ); t_player( 0, "Alice", qtrue, 11 );
	t_write_plugin( "sl", "Commands.add('many', function(p) Storage.set('keep', ('k'):rep(100)) for i = 1, 100 do Storage.char.set(p, 'c' .. i, ('v'):rep(100)) end end) Commands.add('again', function(p) Storage.char.set(p, 'c1', ('w'):rep(100)) end)\n" );
	t_start( "sl" ); p = RP_Lua_Find( "sl" ); t_args( "many" ); RP_LUA_COMMAND( &g_entities[0], "many" );
	CHECK( p->numPending == 101 && p->storeBytes > 100 * 100, "a plugin key and 100 character keys pending" );
	p->lastFlush = level.time - RP_LUA_STORE_FLUSH_MS - 1; t_frame( 50 ); CHECK( p->flushCursor == RP_LUA_STORE_FLUSH_ROWS, "a slice written (cursor %d)", p->flushCursor );
	t_args( "again" ); RP_LUA_COMMAND( &g_entities[0], "again" ); CHECK( p->numPending == 102, "c1 set again behind the cursor: a new entry (%d)", p->numPending );
	RP_LUA_CHAR_REMOVED( 11 ); CHECK( p->storeBytes == 4 + 100 + 32, "the character's rows subtracted once each: %d bytes left (the plugin key)", (int)p->storeBytes );
	RP_LUA_SHUTDOWN();

	// --- AUDIT M6: a character removed while no plugin runs still loses its rows (the store is open whenever rp_lua is on)
	t_init( 8 ); t_start( "" ); CHECK( rp_luaActive == 0, "no plugin" );
	{ sqlite3 *db; sqlite3_open( "./home/" RP_MOD_FOLDER "/database/lua.db", &db ); sqlite3_exec( db, "INSERT INTO char_kv VALUES ('s', 13, 'k', 'v')", 0, 0, NULL ); sqlite3_close( db ); }
	RP_LUA_CHAR_REMOVED( 13 ); CHECK( count_rows( "SELECT COUNT(*) FROM char_kv WHERE char_id = 13" ) == 0, "character 13 rows deleted with no plugin loaded" );
	RP_LUA_SHUTDOWN();

	// --- AUDIT 3 C4: values keep their bytes through the flush and a fresh read from the database; keys with a zero byte are refused
	t_init( 8 ); t_player( 0, "Alice", qtrue, 11 );
	t_write_plugin( "bin", "Commands.add('binset', function(p) Storage.set('v', string.pack('<i4i4', 5, 1000)) Storage.char.set(p, 'w', 'gold\\0silver') end)\n"
		"Commands.add('binget', function(p) local v = Storage.get('v') local w = Storage.char.get(p, 'w') LV = v and #v or -1 LW = w and #w or -1 if v and #v == 8 then A, B = string.unpack('<i4i4', v) end W = w end)\n"
		"Commands.add('zkey', function(p) OKG, EG = pcall(Storage.get, 'a\\0b') OKS, ES = pcall(Storage.set, 'a\\0b', 1) OKC, EC = pcall(Storage.char.set, p, 'x\\0', 'y') EG, ES, EC = tostring(EG), tostring(ES), tostring(EC) end)\n" );
	t_start( "bin" ); p = RP_Lua_Find( "bin" ); CHECK( p && p->L, "bin loaded" );
	t_args( "binset" ); RP_LUA_COMMAND( &g_entities[0], "binset" ); t_args( "binget" ); RP_LUA_COMMAND( &g_entities[0], "binget" );
	{ lua_getglobal( p->L, "LV" ); lua_getglobal( p->L, "LW" ); CHECK( lua_tointeger( p->L, -2 ) == 8 && lua_tointeger( p->L, -1 ) == 11, "pending: %d and %d bytes", (int)lua_tointeger( p->L, -2 ), (int)lua_tointeger( p->L, -1 ) ); lua_pop( p->L, 2 ); }
	for ( i = 0; i < 31; i++ ) t_frame( 1000 );
	CHECK( p->numPending == 0 && count_rows( "SELECT COUNT(*) FROM plugin_kv WHERE plugin = 'bin' AND LENGTH(CAST(value AS BLOB)) = 8" ) == 1, "flushed with its 8 bytes" );
	t_args( "binget" ); RP_LUA_COMMAND( &g_entities[0], "binget" );
	{ lua_getglobal( p->L, "LV" ); lua_getglobal( p->L, "LW" ); lua_getglobal( p->L, "A" ); lua_getglobal( p->L, "B" );
	  CHECK( lua_tointeger( p->L, -4 ) == 8 && lua_tointeger( p->L, -3 ) == 11 && lua_tointeger( p->L, -2 ) == 5 && lua_tointeger( p->L, -1 ) == 1000, "after the flush: %d and %d bytes, unpacked %d %d", (int)lua_tointeger( p->L, -4 ), (int)lua_tointeger( p->L, -3 ), (int)lua_tointeger( p->L, -2 ), (int)lua_tointeger( p->L, -1 ) );
	  lua_pop( p->L, 4 ); }
	{ size_t n; const char *w; lua_getglobal( p->L, "W" ); w = lua_tolstring( p->L, -1, &n ); CHECK( w && n == 11 && !memcmp( w, "gold\0silver", 11 ), "the char value byte for byte" ); lua_pop( p->L, 1 ); }
	RP_LUA_SHUTDOWN(); t_init( 8 ); t_player( 0, "Alice", qtrue, 11 ); t_start( "bin" ); p = RP_Lua_Find( "bin" );
	t_args( "binget" ); RP_LUA_COMMAND( &g_entities[0], "binget" );
	{ lua_getglobal( p->L, "A" ); lua_getglobal( p->L, "B" ); lua_getglobal( p->L, "LW" ); CHECK( lua_tointeger( p->L, -3 ) == 5 && lua_tointeger( p->L, -2 ) == 1000 && lua_tointeger( p->L, -1 ) == 11, "a new map, read from the database: %d %d, %d bytes", (int)lua_tointeger( p->L, -3 ), (int)lua_tointeger( p->L, -2 ), (int)lua_tointeger( p->L, -1 ) ); lua_pop( p->L, 3 ); }
	t_args( "zkey" ); RP_LUA_COMMAND( &g_entities[0], "zkey" );
	CHECK( strstr( get_str( p, "EG" ), "a key has no zero byte" ) && strstr( get_str( p, "ES" ), "a key has no zero byte" ) && strstr( get_str( p, "EC" ), "a key has no zero byte" ), "zero-byte keys refused: %s / %s / %s", get_str( p, "EG" ), get_str( p, "ES" ), get_str( p, "EC" ) );
	CHECK( p->numPending == 0, "nothing written for them" );
	RP_LUA_SHUTDOWN();

	printf( fails ? "%d FAILED\n" : "t_store: ALL OK\n", fails );
	return fails != 0;
}
