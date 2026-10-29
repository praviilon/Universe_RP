/*
===========================================================================
GLX_U: [Lua] stored data: lua.db next to accounts.db, a table of plugin keys and one of per-character
keys (CharID). Writes are kept pending and flushed every RP_LUA_STORE_FLUSH_MS in slices of
RP_LUA_STORE_FLUSH_ROWS rows a frame, so a flush never takes a frame; reads look at the pending
writes first. A plugin's keys and values together stay under RP_LUA_STORE_MAX.
===========================================================================
*/
#include "rp_lua_local.h"
#include "sqlite/sqlite3.h"

static sqlite3 *rpLuaDb = NULL;

static int RP_LuaStore_PendingIndex( rpLuaPlugin_t *p, int charId, const char *key );

// storeBytes less a row: never under zero (the count is rebuilt from the database at each load; a row
// counted differently there than here must not leave the plugin at "storage full" until the next map)
static void RP_LuaStore_Shrink( rpLuaPlugin_t *p, size_t bytes )
{
	p->storeBytes = ( p->storeBytes > bytes ) ? p->storeBytes - bytes : 0;
}

#define RP_LUA_ROW_OVERHEAD 32

qboolean RP_LuaStore_Open( void )
{
	char home[MAX_OSPATH] = { 0 }, path[MAX_OSPATH * 2];
	char *err = NULL;

	trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
	if ( home[0] )
		Com_sprintf( path, sizeof( path ), "%s/%s/database/lua.db", home, RP_MOD_FOLDER );
	else
		Com_sprintf( path, sizeof( path ), "%s/database/lua.db", RP_MOD_FOLDER );

	if ( sqlite3_open( path, &rpLuaDb ) != SQLITE_OK )
	{
		G_LogPrintf( "[lua] could not open %s: %s\n", path, rpLuaDb ? sqlite3_errmsg( rpLuaDb ) : "?" );
		if ( rpLuaDb )
			sqlite3_close( rpLuaDb );
		rpLuaDb = NULL;
		return qfalse;
	}
	sqlite3_busy_timeout( rpLuaDb, 2000 );
	sqlite3_exec( rpLuaDb, "PRAGMA journal_mode=WAL;", 0, 0, NULL );
	if ( sqlite3_exec( rpLuaDb,
		"CREATE TABLE IF NOT EXISTS plugin_kv (plugin TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (plugin, key));"
		"CREATE TABLE IF NOT EXISTS char_kv (plugin TEXT NOT NULL, char_id INTEGER NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (plugin, char_id, key));",
		0, 0, &err ) != SQLITE_OK )
	{
		G_LogPrintf( "[lua] lua.db tables: %s\n", err ? err : "?" );
		sqlite3_free( err );
		sqlite3_close( rpLuaDb );
		rpLuaDb = NULL;
		return qfalse;
	}
	return qtrue;
}

void RP_LuaStore_Close( void )
{
	if ( rpLuaDb )
	{
		sqlite3_close( rpLuaDb );
		rpLuaDb = NULL;
	}
}

// characters removed by hand in accounts.db: their rows go
void RP_LuaStore_SweepOrphans( void )
{
	char home[MAX_OSPATH] = { 0 }, path[MAX_OSPATH * 2];
	sqlite3_stmt *stmt = NULL;

	if ( !rpLuaDb )
		return;
	trap->Cvar_VariableStringBuffer( "fs_homepath", home, sizeof( home ) );
	if ( home[0] )
		Com_sprintf( path, sizeof( path ), "%s/%s", home, DB_PATH );
	else
		Q_strncpyz( path, DB_PATH, sizeof( path ) );

	if ( sqlite3_prepare_v2( rpLuaDb, "ATTACH DATABASE ? AS acc", -1, &stmt, NULL ) != SQLITE_OK )
		return;
	sqlite3_bind_text( stmt, 1, path, -1, SQLITE_TRANSIENT );
	if ( sqlite3_step( stmt ) != SQLITE_DONE )
	{
		sqlite3_finalize( stmt );
		return;
	}
	sqlite3_finalize( stmt );
	if ( sqlite3_exec( rpLuaDb, "DELETE FROM char_kv WHERE char_id NOT IN (SELECT CharID FROM acc.Characters)", 0, 0, NULL ) == SQLITE_OK )
	{
		int n = sqlite3_changes( rpLuaDb );

		if ( n )
			G_LogPrintf( "[lua] %d stored values of removed characters deleted\n", n );
	}
	sqlite3_exec( rpLuaDb, "DETACH DATABASE acc", 0, 0, NULL );
}

void RP_LuaStore_CharacterRemoved( int charId )
{
	sqlite3_stmt *stmt = NULL;
	int i;

	for ( i = 0; i < RP_LUA_MAX_PLUGINS; i++ )
	{ // and what is still pending for that character
		rpLuaPlugin_t *p = &rpLuaPlugins[i];
		int k;

		if ( !p->inUse || !p->pending )
			continue;
		for ( k = 0; k < p->numPending; k++ )
		{
			if ( p->pending[k].charId == charId && p->pending[k].value )
			{
				if ( RP_LuaStore_PendingIndex( p, charId, p->pending[k].key ) == k )	// the latest entry of the key: the one counted
					RP_LuaStore_Shrink( p, strlen( p->pending[k].key ) + p->pending[k].valueLen + RP_LUA_ROW_OVERHEAD );
				free( p->pending[k].value );
				p->pending[k].value = NULL;
				p->pending[k].valueLen = 0;
			}
		}
	}
	if ( !rpLuaDb || sqlite3_prepare_v2( rpLuaDb, "DELETE FROM char_kv WHERE char_id = ?", -1, &stmt, NULL ) != SQLITE_OK )
		return;
	sqlite3_bind_int( stmt, 1, charId );
	sqlite3_step( stmt );
	sqlite3_finalize( stmt );
}

/*
==================
Per plugin
==================
*/
void RP_LuaStore_PluginInit( rpLuaPlugin_t *p )
{
	sqlite3_stmt *stmt = NULL;

	p->storeBytes = 0;
	p->numPending = 0;
	p->flushCursor = -1;
	p->lastFlush = level.time;
	if ( !rpLuaDb )
		return;
	if ( sqlite3_prepare_v2( rpLuaDb,
		"SELECT COALESCE((SELECT SUM(LENGTH(CAST(key AS BLOB)) + LENGTH(CAST(value AS BLOB)) + 32) FROM plugin_kv WHERE plugin = ?1), 0)"
		" + COALESCE((SELECT SUM(LENGTH(CAST(key AS BLOB)) + LENGTH(CAST(value AS BLOB)) + 32) FROM char_kv WHERE plugin = ?1), 0)", -1, &stmt, NULL ) == SQLITE_OK )	// bytes, as everywhere else (LENGTH of a TEXT counts characters)
	{
		sqlite3_bind_text( stmt, 1, p->name, -1, SQLITE_STATIC );
		if ( sqlite3_step( stmt ) == SQLITE_ROW )
			p->storeBytes = (size_t)sqlite3_column_int64( stmt, 0 );
		sqlite3_finalize( stmt );
	}
}

// the row in the database: its value length, or -1 for none (value copied into out when given)
static int RP_LuaStore_DbGet( rpLuaPlugin_t *p, int charId, const char *key, char *out, int outSize )
{
	sqlite3_stmt *stmt = NULL;
	int len = -1;

	if ( !rpLuaDb )
		return -1;
	if ( charId )
	{
		if ( sqlite3_prepare_v2( rpLuaDb, "SELECT value FROM char_kv WHERE plugin = ? AND char_id = ? AND key = ?", -1, &stmt, NULL ) != SQLITE_OK )
			return -1;
		sqlite3_bind_text( stmt, 1, p->name, -1, SQLITE_STATIC );
		sqlite3_bind_int( stmt, 2, charId );
		sqlite3_bind_text( stmt, 3, key, -1, SQLITE_STATIC );
	}
	else
	{
		if ( sqlite3_prepare_v2( rpLuaDb, "SELECT value FROM plugin_kv WHERE plugin = ? AND key = ?", -1, &stmt, NULL ) != SQLITE_OK )
			return -1;
		sqlite3_bind_text( stmt, 1, p->name, -1, SQLITE_STATIC );
		sqlite3_bind_text( stmt, 2, key, -1, SQLITE_STATIC );
	}
	if ( sqlite3_step( stmt ) == SQLITE_ROW )
	{
		const char *v = (const char *)sqlite3_column_text( stmt, 0 );

		len = sqlite3_column_bytes( stmt, 0 );
		if ( out )
			Q_strncpyz( out, v ? v : "", outSize );
	}
	sqlite3_finalize( stmt );
	return len;
}

// the pending write for a key, the latest one, or -1
static int RP_LuaStore_PendingIndex( rpLuaPlugin_t *p, int charId, const char *key )
{
	int i;

	for ( i = p->numPending - 1; i >= 0; i-- )
	{
		if ( p->pending[i].charId == charId && !strcmp( p->pending[i].key, key ) )
			return i;
	}
	return -1;
}

// the current size of a key's row, pending or stored: 0 for none
static size_t RP_LuaStore_RowSize( rpLuaPlugin_t *p, int charId, const char *key )
{
	int i = RP_LuaStore_PendingIndex( p, charId, key ), len;

	if ( i >= 0 )
		return p->pending[i].value ? strlen( key ) + p->pending[i].valueLen + RP_LUA_ROW_OVERHEAD : 0;
	len = RP_LuaStore_DbGet( p, charId, key, NULL, 0 );
	return ( len >= 0 ) ? strlen( key ) + (size_t)len + RP_LUA_ROW_OVERHEAD : 0;
}

// value NULL: a delete. qfalse with why when refused.
static qboolean RP_LuaStore_Set( rpLuaPlugin_t *p, int charId, const char *key, const char *value, size_t valueLen, const char **why )
{
	size_t oldSize = RP_LuaStore_RowSize( p, charId, key );
	size_t newSize = value ? strlen( key ) + valueLen + RP_LUA_ROW_OVERHEAD : 0;
	int i;
	rpLuaPending_t *e;

	if ( !rpLuaDb )
	{
		*why = "storage is not available";
		return qfalse;
	}
	if ( ( p->storeBytes > oldSize ? p->storeBytes - oldSize : 0 ) + newSize > RP_LUA_STORE_MAX )
	{
		*why = "storage full";
		return qfalse;
	}
	if ( !p->pending )
	{
		p->pending = (rpLuaPending_t *)calloc( RP_LUA_PENDING_MAX, sizeof( rpLuaPending_t ) );
		if ( !p->pending )
		{
			*why = "out of memory";
			return qfalse;
		}
	}
	i = RP_LuaStore_PendingIndex( p, charId, key );
	if ( i >= 0 && i < p->flushCursor )
		i = -1;	// that entry is written already: a new one
	if ( i < 0 )
	{
		if ( p->numPending >= RP_LUA_PENDING_MAX )
		{
			*why = "too many pending writes";
			return qfalse;
		}
		i = p->numPending++;
		e = &p->pending[i];
		memset( e, 0, sizeof( *e ) );
		Q_strncpyz( e->key, key, sizeof( e->key ) );
		e->charId = charId;
	}
	else
	{
		e = &p->pending[i];
		free( e->value );
		e->value = NULL;
	}
	if ( value )
	{
		e->value = (char *)malloc( valueLen + 1 );
		if ( !e->value )
		{
			*why = "out of memory";
			return qfalse;
		}
		memcpy( e->value, value, valueLen );
		e->value[valueLen] = '\0';
		e->valueLen = (int)valueLen;
	}
	else
		e->valueLen = 0;
	RP_LuaStore_Shrink( p, oldSize );
	p->storeBytes += newSize;
	return qtrue;
}

static void RP_LuaStore_WriteRow( rpLuaPlugin_t *p, const rpLuaPending_t *e )
{
	sqlite3_stmt *stmt = NULL;
	const char *sql;

	if ( e->charId )
		sql = e->value ? "INSERT OR REPLACE INTO char_kv (plugin, char_id, key, value) VALUES (?1, ?2, ?3, ?4)"
					   : "DELETE FROM char_kv WHERE plugin = ?1 AND char_id = ?2 AND key = ?3";
	else
		sql = e->value ? "INSERT OR REPLACE INTO plugin_kv (plugin, key, value) VALUES (?1, ?3, ?4)"
					   : "DELETE FROM plugin_kv WHERE plugin = ?1 AND key = ?3";
	if ( sqlite3_prepare_v2( rpLuaDb, sql, -1, &stmt, NULL ) != SQLITE_OK )
		return;
	sqlite3_bind_text( stmt, 1, p->name, -1, SQLITE_STATIC );
	if ( e->charId )
		sqlite3_bind_int( stmt, 2, e->charId );
	sqlite3_bind_text( stmt, 3, e->key, -1, SQLITE_STATIC );
	if ( e->value )
		sqlite3_bind_text( stmt, 4, e->value, e->valueLen, SQLITE_STATIC );
	sqlite3_step( stmt );
	sqlite3_finalize( stmt );
}

// rows pending written, at most max of them; qtrue when nothing is left
static qboolean RP_LuaStore_FlushSome( rpLuaPlugin_t *p, int max )
{
	int n = 0;

	if ( !rpLuaDb || !p->pending )
		return qtrue;
	if ( p->flushCursor < 0 )
		p->flushCursor = 0;
	sqlite3_exec( rpLuaDb, "BEGIN", 0, 0, NULL );
	while ( p->flushCursor < p->numPending && n < max )
	{
		RP_LuaStore_WriteRow( p, &p->pending[p->flushCursor] );
		p->flushCursor++;
		n++;
	}
	sqlite3_exec( rpLuaDb, "COMMIT", 0, 0, NULL );
	if ( p->flushCursor >= p->numPending )
	{
		int i;

		for ( i = 0; i < p->numPending; i++ )
			free( p->pending[i].value );
		p->numPending = 0;
		p->flushCursor = -1;
		p->lastFlush = level.time;
		return qtrue;
	}
	return qfalse;
}

void RP_LuaStore_Frame( rpLuaPlugin_t *p )
{
	if ( !p->numPending )
		return;
	if ( p->flushCursor >= 0 || level.time - p->lastFlush >= RP_LUA_STORE_FLUSH_MS )
		RP_LuaStore_FlushSome( p, RP_LUA_STORE_FLUSH_ROWS );
}

void RP_LuaStore_PluginClose( rpLuaPlugin_t *p )
{
	while ( !RP_LuaStore_FlushSome( p, 1024 ) )
		;
}

/*
==================
The Lua side
==================
*/
static const char *RP_LuaStore_KeyArg( lua_State *L, int idx )
{
	size_t len;
	const char *key = luaL_checklstring( L, idx, &len );

	if ( !len || len > RP_LUA_KEY_MAX )
		luaL_error( L, "a key is 1 to %d bytes", RP_LUA_KEY_MAX );
	return key;
}

static int RP_LuaStore_Get( lua_State *L, int charId, int keyIdx )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *key = RP_LuaStore_KeyArg( L, keyIdx );
	char value[RP_LUA_VALUE_MAX + 1];
	int i = p->pending ? RP_LuaStore_PendingIndex( p, charId, key ) : -1;

	if ( i >= 0 )
	{
		if ( p->pending[i].value )
			lua_pushlstring( L, p->pending[i].value, p->pending[i].valueLen );
		else
			lua_pushnil( L );
		return 1;
	}
	if ( RP_LuaStore_DbGet( p, charId, key, value, sizeof( value ) ) < 0 )
		lua_pushnil( L );
	else
		lua_pushstring( L, value );
	return 1;
}

static int RP_LuaStore_SetLua( lua_State *L, int charId, int keyIdx )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *key = RP_LuaStore_KeyArg( L, keyIdx );
	size_t len;
	const char *value;
	const char *why = NULL;

	if ( lua_type( L, keyIdx + 1 ) != LUA_TSTRING && lua_type( L, keyIdx + 1 ) != LUA_TNUMBER )
		return luaL_error( L, "a stored value is a string or a number" );
	value = lua_tolstring( L, keyIdx + 1, &len );
	if ( len > RP_LUA_VALUE_MAX )
		return luaL_error( L, "a stored value is at most %d bytes", RP_LUA_VALUE_MAX );
	if ( !RP_LuaStore_Set( p, charId, key, value, len, &why ) )
	{
		lua_pushboolean( L, 0 );
		lua_pushstring( L, why );
		return 2;
	}
	lua_pushboolean( L, 1 );
	return 1;
}

static int RP_LuaStore_DelLua( lua_State *L, int charId, int keyIdx )
{
	rpLuaPlugin_t *p = RP_LUA_PLUGIN( L );
	const char *key = RP_LuaStore_KeyArg( L, keyIdx );
	const char *why = NULL;

	if ( !RP_LuaStore_Set( p, charId, key, NULL, 0, &why ) )
	{
		lua_pushboolean( L, 0 );
		lua_pushstring( L, why );
		return 2;
	}
	lua_pushboolean( L, 1 );
	return 1;
}

static int RP_LuaStore_CharId( lua_State *L )
{
	int clientNum;
	gentity_t *ent;

	RP_LuaApi_PlayerArg( L, 1, &clientNum );
	ent = &g_entities[clientNum];
	if ( !ent->client->sess.loggedin || ent->client->pers.CharID <= 0 )
		return luaL_error( L, "the player has no character" );
	return ent->client->pers.CharID;
}

static int RP_LuaStore_PluginGet( lua_State *L )	{ return RP_LuaStore_Get( L, 0, 1 ); }
static int RP_LuaStore_PluginSet( lua_State *L )	{ return RP_LuaStore_SetLua( L, 0, 1 ); }
static int RP_LuaStore_PluginDel( lua_State *L )	{ return RP_LuaStore_DelLua( L, 0, 1 ); }
static int RP_LuaStore_CharGet( lua_State *L )		{ return RP_LuaStore_Get( L, RP_LuaStore_CharId( L ), 2 ); }
static int RP_LuaStore_CharSet( lua_State *L )		{ return RP_LuaStore_SetLua( L, RP_LuaStore_CharId( L ), 2 ); }
static int RP_LuaStore_CharDel( lua_State *L )		{ return RP_LuaStore_DelLua( L, RP_LuaStore_CharId( L ), 2 ); }

static const luaL_Reg rpLuaStorage[] = {
	{ "get", RP_LuaStore_PluginGet }, { "set", RP_LuaStore_PluginSet }, { "del", RP_LuaStore_PluginDel }, { NULL, NULL }
};
static const luaL_Reg rpLuaStorageChar[] = {
	{ "get", RP_LuaStore_CharGet }, { "set", RP_LuaStore_CharSet }, { "del", RP_LuaStore_CharDel }, { NULL, NULL }
};

void RP_LuaStore_OpenLib( rpLuaPlugin_t *p )
{
	lua_State *L = p->L;

	luaL_newlib( L, rpLuaStorage );
	luaL_newlib( L, rpLuaStorageChar );
	lua_setfield( L, -2, "char" );
	lua_setglobal( L, "Storage" );
}
