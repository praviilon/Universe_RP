/*
===========================================================================
GLX_U: [Lua] shared by the rp_lua_*.c files only. The limits are here with the reason for each; the
three cvars (rp_lua, rp_lua_plugins, rp_lua_memkb) are in g_xcvar.h.
===========================================================================
*/
#pragma once

#include "../../codemp/game/g_local.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "rp_lua.h"

#define RP_LUA_MAX_PLUGINS		16
#define RP_LUA_NAME_LEN			32		// a plugin folder name
#define RP_LUA_CALL_MS			10		// one script call on the real clock, then "ran too long"
#define RP_LUA_CALL_INSTRUCTIONS 1000000	// and its instruction cap, counted 1000 at a time
#define RP_LUA_HOOK_COUNT		100	// the count hook runs every this many VM instructions
#define RP_LUA_PLUGIN_FRAME_MS	20		// all of a plugin's calls in one frame; timers past it wait, events run with
#define RP_LUA_CALL_SHORT_MS	1		// this deadline, and past twice the budget are skipped (init and shutdown never)
#define RP_LUA_FRAME_MS			50		// all plugins in one frame
#define RP_LUA_MAX_ERRORS		20		// errors in RP_LUA_ERROR_WINDOW_MS that switch a plugin off
#define RP_LUA_ERROR_WINDOW_MS	60000
#define RP_LUA_NOTICE_MS		30000	// an error reaches online Scripts admins at most this often per plugin
#define RP_LUA_TEXT_MAX			900		// a message or log line (the engine's reliable command is 1024)
#define RP_LUA_TOKENS			4		// the command bucket: a player sends one reliable command per
										// sv_floodProtect ms; a plugin gets the same rate with this burst
#define RP_LUA_SENDS_PER_SEC	4		// script commands to one player per second, all plugins together
										// (Msg.*, and every World.use: its activator's room)
#define RP_LUA_CMDS_PER_SEC		32		// any command to one client from inside script calls, a second, all plugins
										// together (the engine drops a client 128 behind)
#define RP_LUA_DB_WRITES_FRAME	4		// database writes from inside script calls, a server frame (a payout)
#define RP_LUA_BROADCAST_PER_TOKEN 8	// recipients one token buys in Msg.broadcast
#define RP_LUA_MAX_TIMERS		32		// live timers per plugin
#define RP_LUA_TIMER_MIN_MS		50
#define RP_LUA_MAX_WATCHES		32		// watched targetnames per plugin
#define RP_LUA_MAX_WATCHES_ALL	256
#define RP_LUA_MAX_COMMANDS		16		// script commands per plugin
#define RP_LUA_CMD_NAME_MAX		24
#define RP_LUA_CMD_HELP_MAX		96
#define RP_LUA_MAX_ARGS			16
#define RP_LUA_EVENT_FRAME_MS	50		// the frame event's period
#define RP_LUA_KEY_MAX			64		// storage
#define RP_LUA_VALUE_MAX		4096
#define RP_LUA_STORE_MAX		( 1024 * 1024 )	// per plugin, keys and values together
#define RP_LUA_STORE_FLUSH_MS	30000
#define RP_LUA_STORE_FLUSH_ROWS	64		// rows written per frame of a flush
#define RP_LUA_PENDING_MAX		2048	// pending writes per plugin (bounded by RP_LUA_STORE_MAX anyway)
#define RP_LUA_RADIUS_MAX		8192.0f
// P2: the world
#define RP_LUA_MAX_PROPS		32		// a plugin's props alive at once
#define RP_LUA_MAX_NPCS			8		// and its NPCs
#define RP_LUA_MAX_VEHICLES		6		// and its vehicles (World.vehicle), apart from the NPCs
#define RP_LUA_MAX_ENTS			( RP_LUA_MAX_PROPS + RP_LUA_MAX_NPCS + RP_LUA_MAX_VEHICLES )
#define RP_LUA_VEHICLE_SPAWNS_MAP	24	// vehicle spawns per plugin per map (each leaves its spawner's strings in the pool)
#define RP_LUA_VEHICLES_SERVER	24		// script vehicles alive on the server, all plugins and released ones together
#define RP_LUA_RELEASED			( RP_LUA_MAX_PLUGINS + 1 )	// gentity_t::rpLuaPlugin of a ridden vehicle its plugin let go:
												// no plugin's, still a script's (/entsave leaves it out)
#define RP_LUA_PROP_SPAWNS_MAP	128		// spawns per plugin per map: the key/value pool (G_Alloc) and the
#define RP_LUA_NPC_SPAWNS_MAP	32		// spawner strings are never freed within a map
#define RP_LUA_DIALOG_SPAWNS_MAP 16		// target_dialogs per plugin per map, within the 128: each keeps ~1.5 KB of pool
#define RP_LUA_NPC_HEALTH_MAX	100000	// World.npc's health option: 1..this
#define RP_LUA_MAX_KEYS			24		// key/value pairs a World.prop may carry (ZYK_MAX_SPAWN_STRING_SLOTS/2 is 64)
#define RP_LUA_KEY_NAME_MAX		32
#define RP_LUA_KEY_VALUE_MAX	256
#define RP_LUA_ENT_NAME_MAX		32		// the <name> part of lua.<plugin>.<name>
#define RP_LUA_MAP_SIZE			131072.0f	// an origin past this is refused (MAX_MAP_AREA_BYTES-scale maps are 2^17)

typedef enum {
	RPLE_INIT, RPLE_SHUTDOWN, RPLE_FRAME, RPLE_CLIENT_BEGIN, RPLE_CLIENT_DISCONNECT, RPLE_CHAT,
	RPLE_PLAYER_DEATH, RPLE_LOGIN, RPLE_LOGOUT, RPLE_CHARACTER_SELECT, RPLE_LEVEL_UP, RPLE_ENTITY_USED,
	RPLE_NPC_DEATH, RPLE_DIALOG_ANSWER,
	RPLE_COUNT
} rpLuaEvent_t;

extern const char *rpLuaEventNames[RPLE_COUNT];

typedef struct {
	int			id;
	int			due;		// level.time
	int			interval;	// 0: once
	int			ref;		// registry ref of the function
} rpLuaTimer_t;

typedef struct {
	char		key[RP_LUA_KEY_MAX + 1];
	int			charId;		// 0: a plugin key
	char		*value;		// NULL: a delete
	int			valueLen;
} rpLuaPending_t;

typedef enum { RPLOP_NONE, RPLOP_DISABLE, RPLOP_UNLOAD, RPLOP_RELOAD } rpLuaOp_t;

// P2: an entity a plugin spawned. Live while g_entities[number] is in use with this stamp.
typedef struct {
	int			number;
	int			stamp;
	qboolean	npc;
	qboolean	vehicle;		// an NPC that is a vehicle (World.vehicle): counted and removed apart
	qboolean	removeQueued;	// World.remove: freed at the frame end
} rpLuaEntity_t;

typedef struct rpLuaPlugin_s {
	char			name[RP_LUA_NAME_LEN + 1];
	qboolean		inUse;		// slot taken
	qboolean		disabled;	// state closed after errors or /scripts disable; name kept
	lua_State		*L;

	// memory
	size_t			memUsed, memPeak;
	size_t			memCap;			// rp_lua_memkb when the state was made: a change applies at the next load

	// the call in progress
	int				deadline;		// Milliseconds
	int				instructions;
	qboolean		inCall;
	qboolean		killed;			// past the budget: every instruction raises until the call unwinds (a pcall cannot keep it)
	char			killWhy[48];	// the reason, raised again each time
	int				deadlineMs;		// RP_LUA_CALL_MS, or RP_LUA_CALL_SHORT_MS past the frame budget

	// budget
	int				tokens;
	int				tokenRefill;	// level.time of the next token
	int				frameMsLeft;
	int				errorTimes[RP_LUA_MAX_ERRORS];
	int				errorHead;
	int				lastNotice;
	int				lastSkipLog;	// level.time of the last "event not run" line

	// what the script registered
	int				handlers[RPLE_COUNT];	// registry refs, LUA_NOREF for none
	rpLuaTimer_t	timers[RP_LUA_MAX_TIMERS];
	int				numTimers, nextTimerId;
	char			watches[RP_LUA_MAX_WATCHES][MAX_QPATH];
	int				numWatches;
	int				frameDue;		// level.time of the next frame event
	char			cmdNames[RP_LUA_MAX_COMMANDS][RP_LUA_CMD_NAME_MAX + 1];
	char			cmdHelp[RP_LUA_MAX_COMMANDS][RP_LUA_CMD_HELP_MAX + 1];
	int				cmdRefs[RP_LUA_MAX_COMMANDS];
	int				numCommands;

	// storage
	rpLuaPending_t	*pending;
	int				numPending;
	size_t			storeBytes;		// as in lua.db plus pending
	int				lastFlush;
	int				flushCursor;	// pending index a slice flush got to (-1: no flush running)

	// queued for the end of the frame
	rpLuaOp_t		pendingOp;

	// P2: the world
	rpLuaEntity_t	ents[RP_LUA_MAX_ENTS];
	int				numEnts;
	int				propSpawns, npcSpawns, vehicleSpawns, dialogSpawns;	// this map (this load)
	qboolean		removesQueued;

	// stats
	int				calls, callMs, errorsTotal, refusedTokens, refusedRecipients, deferredTimers, sent;
	int				uses, effects, sounds, spawnsRefused;
	int				usesBusy, cmdsHeld, dbRefused;	// the stability guards (rp_lua_api.c)
	int				eventsShort, eventsSkipped;		// event calls past the frame budget: with the 1 ms deadline, and not made
	int				luaUsesHeld;					// target_lua uses made from inside this plugin's calls, refused
} rpLuaPlugin_t;

extern rpLuaPlugin_t rpLuaPlugins[RP_LUA_MAX_PLUGINS];
extern int rpLuaFrameMsLeft;
extern int rpLuaGen[MAX_CLIENTS];
extern rpLuaPlugin_t *rpLuaCurrent;	// the plugin whose call runs, for the API

// core
rpLuaPlugin_t *RP_Lua_Find( const char *name );
qboolean RP_Lua_Call( rpLuaPlugin_t *p, int nargs, int nresults );
// the registry function ref called with what push() puts on the stack (it returns the count), all inside the
// protected call: nothing is pushed onto a state outside one (at the memory cap that would end the process)
qboolean RP_Lua_CallRef( rpLuaPlugin_t *p, int ref, int ms, int (*push)( lua_State *L, void *ctx ), void *ctx );
int RP_Lua_CallMs( rpLuaPlugin_t *p );	// an event's or command's deadline: short once the frame budget is spent
void RP_Lua_LoopCheck( lua_State *L );	// lstrlib.c and ltablib.c call it (GLX_U): C-side loops the count hook cannot see
rpLuaPlugin_t *RP_Lua_NoPlugin( lua_State *L );	// raises: an API function reached with no script call on the stack
#define RP_LUA_PLUGIN( L )	( rpLuaCurrent ? rpLuaCurrent : RP_Lua_NoPlugin( L ) )
void RP_Lua_Error( rpLuaPlugin_t *p, const char *msg );
qboolean RP_Lua_QueueOp( const char *name, rpLuaOp_t op );
qboolean RP_Lua_QueueLoad( const char *name );
qboolean RP_Lua_PluginFileExists( const char *name );	// lua/plugins/<name>/plugin.lua in the mod folder
void RP_Lua_FireEvent( rpLuaEvent_t ev, void (*fill)( lua_State *L, void *ctx ), void *ctx );
void RP_Lua_RefillTokens( rpLuaPlugin_t *p );
void RP_Lua_FirePlugin( rpLuaPlugin_t *p, rpLuaEvent_t ev, void (*fill)( lua_State *L, void *ctx ), void *ctx );	// one plugin's handler
qboolean RP_Lua_EventRoom( rpLuaPlugin_t *p, rpLuaEvent_t ev );
int RP_Lua_PushEmptyEvent( lua_State *L, void *ctx );	// a push() for RP_Lua_CallRef: {}
qboolean RP_Lua_OpenScratch( rpLuaPlugin_t *p, const char *name );
void RP_Lua_CloseScratch( rpLuaPlugin_t *p );

// api
void RP_LuaApi_Open( rpLuaPlugin_t *p );
void RP_LuaApi_Reset( void );
void RP_LuaApi_PushPlayer( lua_State *L, int clientNum );
qboolean RP_LuaApi_PlayerArg( lua_State *L, int idx, int *clientNum );
void RP_LuaApi_RunTimers( rpLuaPlugin_t *p );
void RP_LuaApi_ClosePlugin( rpLuaPlugin_t *p );
qboolean RP_LuaApi_TakeToken( rpLuaPlugin_t *p, int n );
qboolean RP_LuaApi_SendRoom( int clientNum );
void RP_LuaApi_Sent( int clientNum );

// world (P2)
void RP_LuaWorld_OpenLib( rpLuaPlugin_t *p );
void RP_LuaWorld_Frame( rpLuaPlugin_t *p );		// queued removes
void RP_LuaWorld_PluginClose( rpLuaPlugin_t *p );	// every entity it made, now (frame end or map end only)
int RP_LuaWorld_CountLive( rpLuaPlugin_t *p, qboolean npcs );	// props or NPCs, vehicles not included
int RP_LuaWorld_CountVehicles( rpLuaPlugin_t *p );
void RP_LuaWorld_ReleasedFrame( void );	// every frame: released vehicles nobody rides any more are removed
void RP_LuaWorld_Reset( void );			// a new map: no released vehicle survives it
rpLuaPlugin_t *RP_LuaWorld_OwnerOf( const gentity_t *ent );	// the plugin that spawned it, or NULL
void RP_LuaWorld_PushEntity( lua_State *L, const gentity_t *ent );	// a handle for an entity it owns

// store
qboolean RP_LuaStore_Open( void );
void RP_LuaStore_Close( void );
void RP_LuaStore_PluginInit( rpLuaPlugin_t *p );
void RP_LuaStore_PluginClose( rpLuaPlugin_t *p );	// flushes everything
void RP_LuaStore_Frame( rpLuaPlugin_t *p );
void RP_LuaStore_OpenLib( rpLuaPlugin_t *p );
void RP_LuaStore_CharacterRemoved( int charId );
void RP_LuaStore_SweepOrphans( void );

// admin
void RP_LuaAdmin_Notice( rpLuaPlugin_t *p, const char *text );
