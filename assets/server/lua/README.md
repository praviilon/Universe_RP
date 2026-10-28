# Server-side Lua plugins (P1)

Universe RP's server can run small Lua 5.4 scripts called plugins. A plugin
is told what happens on the server and can answer with messages, chat
commands, timers and stored data. It cannot change the world: nothing a
plugin does spawns, moves, damages or frees anything, and a plugin that
misbehaves is cut off by its own budget, never the server. This is the
first stage of the scripting layer; the surface grows once this one has
run on real servers for a while.

## Setup

| Cvar | Default | Meaning |
|---|---|---|
| `rp_lua` | `1` | `0` turns the whole layer off (latched: takes effect at the next map) |
| `rp_lua_plugins` | `""` | plugin folder names to load at map start, separated by spaces or commas |
| `rp_lua_memkb` | `512` | memory per plugin in KB (128–4096) |

A plugin is one file: `<fs_homepath>/<mod folder>/lua/plugins/<name>/plugin.lua`,
a loose file (never inside a pk3), read as text (bytecode is refused).
`<name>` is letters, digits, `_` and `-`, up to 32 characters. The two
shipped examples are `greeter` and `events`: copy `lua/` from
`assets/server/` next to the mod's `database/` folder and set
`rp_lua_plugins "greeter events"`.

Stored data goes to `<mod folder>/database/lua.db`, beside `accounts.db`.

## Admin commands

`/scripts` needs the admin power **Scripts** (bit 29 in the calculator):

| Command | Does |
|---|---|
| `/scripts` or `/scripts list` | each plugin: running or off, memory, errors, timers, commands, watches |
| `/scripts stats [name]` | calls, time, messages sent and refused, deferred timers, stored bytes |
| `/scripts help` | every command the plugins add, with its help text |
| `/scripts load <name>` | load a plugin (for this map; `rp_lua_plugins` is not changed) |
| `/scripts unload <name>` | stop it and free its slot |
| `/scripts reload <name>` or `all` | restart with a fresh state (re-reads the file) |
| `/scripts disable <name>` | stop it, keep the slot (what 20 errors in a minute does) |
| `/scripts enable <name>` | start it again with a fresh error count |

Loads, unloads and reloads happen at the end of the current server frame,
never inside a running script.

From rcon: `lua_list [subcommand]` (the whole `/scripts` from the console),
`lua_reload [name]`, and `lua <code>`, which runs one line in a throwaway
state under the same rules and prints what it returns.

Errors go to the server log as `[lua:<name>] ...` with a traceback, and to
online admins with the Scripts power at most once per 30 s per plugin.

## Limits (the budget)

Every limit is per plugin unless noted. They are there so that a plugin,
however written, costs the server a bounded amount.

| What | Limit | Over it |
|---|---|---|
| one call (an event, a command, a timer) | 10 ms, 1,000,000 instructions | the call ends with "ran too long" |
| all calls in one frame | 20 ms (50 ms for all plugins together) | timers wait for the next frame; nothing is dropped |
| memory | `rp_lua_memkb` | "not enough memory" in the call |
| errors | 20 in 60 s | the plugin is switched off (`/scripts enable` brings it back) |
| messages (`Msg.*`) | a bucket of 4, one token back per `sv_floodProtect` ms | `false, "too fast"` |
| messages to one player | 4 a second across all plugins | `false, "player busy"` |
| timers | 32 live, 50 ms minimum | `false, "too many timers"` |
| commands | 16, names of 3–24 characters | `false, "..."` |
| watched targetnames | 32 (256 for all plugins) | `false, "..."` |
| text | 900 bytes per message or log line | error |
| storage | keys 64 bytes, values 4 KB, 1 MB per plugin | `false, "storage full"` |

A `Msg.broadcast` costs `ceil(recipients / 8)` tokens; `Msg.print` and
`Msg.center` cost one each. Plugin commands cost nothing: the player's own
flood protection paces them.

## The API

Everything a plugin can call. Functions that refuse return `false, "why"`;
wrong arguments raise an error.

### Events

```lua
Events.on("chat", function(e) ... end)   -- one handler per event; a second call replaces it
```

| Event | Fields of `e` |
|---|---|
| `init` | — (after the file ran) |
| `shutdown` | `reason`: `map_end`, `unloaded`, `reloaded`, `disabled` |
| `frame` | — every 50 ms (a timer, so it waits like one) |
| `client_begin` | `player` |
| `client_disconnect` | `player` (still readable inside the handler) |
| `chat` | `player`, `mode` (`say`, `team`, `tell`, `ally`, `ooc`), `text` (as typed, colour codes kept), `target` (tells) |
| `player_death` | `victim`, `killer` (a player, or nil), `mod` (means of death number) |
| `login`, `logout` | `player`, `account` |
| `character_select` | `player`, `account`, `char_id` |
| `level_up` | `player`, `account`, `level` |
| `entity_used` | `targetname`, `entity` (`number`, `classname`), `activator` (a player, or nil) — only for targetnames the plugin watches |

### Players

A `player` value is a handle. When that client leaves, the handle goes
stale: its fields read `nil` and `p:valid()` is false, so keep handles
only briefly, or check them.

Fields (read-only): `id`, `name` (coloured), `clean_name`, `account` (nil
when not logged in), `char_id` (0 for none), `level`, `credits`, `health`,
`team`, `is_bot`, `origin` (`{x, y, z}`).
Methods: `p:valid()`, `p:is_admin("Scripts")` (any power by its
`/adminlist` title).

`Players.all()` — the connected, begun, non-bot players; `Players.get(id)`.

### Messages

```lua
Msg.print(p, text)      -- a line in the console/chat area
Msg.center(p, text)     -- centre-print
Msg.broadcast(text)     -- a line to everyone
```

Newlines become spaces and `"` becomes `'`: one call is one line.

### Commands

```lua
Commands.add("events", function(p, args) ... end, "the next announcements")
```

`args` is a table of the words after the command (at most 16). A name a
built-in command has, or another plugin took, is refused. `/scripts help`
lists them.

### Timers

```lua
local h = Timer.every(60000, fn)   -- also Timer.after(ms, fn); Timer.cancel(h)
```

### World, Cvar, Log, Storage

```lua
World.players_within({x=0, y=0, z=0}, 512)   -- radius up to 8192
World.time()                                 -- the server's level time, ms
World.mapname()
World.entity("door1")                        -- {number, classname, origin} of the first match, or nil
World.watch("door1")  World.unwatch("door1") -- for entity_used
Cvar.get("g_gametype")                       -- rp_, g_ and sv_ cvars only, as strings
Log.info("text")                             -- the server log, as [lua:<name>] text; so does print()
Storage.get(key)  Storage.set(key, value)  Storage.del(key)              -- per plugin
Storage.char.get(p, key)  Storage.char.set(p, key, value)  Storage.char.del(p, key)   -- per character
```

Stored values are strings (numbers are stored as their text; use
`tonumber`). Writes are kept in memory and written every 30 s and at map
end. Rows of a deleted character are removed with it.

### What is not there

No `io`, `os`, `package`, `debug` or `coroutine`; no `require`, `dofile`
or `loadfile`; `load` takes text only; `collectgarbage` only answers
`"count"`. Plugins cannot call each other, use entities, give items, move
players or change cvars. That is by design for this stage.

## Writing a plugin

Start from `plugins/greeter/plugin.lua` (storage and events) or
`plugins/events/plugin.lua` (timers, a command, the message bucket). Keep
each handler short: the budget is 10 ms a call, and anything heavier
belongs in a timer that does a slice at a time. Check the result of
`Msg.*` and `Storage.*` calls when it matters, and look at
`/scripts stats` on a busy server to see what was refused.
