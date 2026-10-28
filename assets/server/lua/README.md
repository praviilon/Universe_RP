# Server-side Lua plugins (P1 + P2)

Universe RP's server can run small Lua 5.4 scripts called plugins. A plugin
is told what happens on the server and can answer with messages, chat
commands, timers and stored data (P1), and act on the world the way a
player would (P2): use a named entity as a button would, place props and
NPCs of its own, play effects and sounds the map already has. It never
reaches past that: no accounts, credits or levels, no kicking, renaming or
damaging anyone, no giving or taking from a player — what a script wants
done to a player, the map's own mechanics do (a `target_give`, a
`target_teleporter`, a `trigger_hurt`), with that player as activator. A
plugin that misbehaves is cut off by its own budget, never the server.

## Setup

| Cvar | Default | Meaning |
|---|---|---|
| `rp_lua` | `1` | `0` turns the whole layer off (latched: takes effect at the next map) |
| `rp_lua_plugins` | `""` | plugin folder names to load at map start, separated by spaces or commas |
| `rp_lua_memkb` | `512` | memory per plugin in KB (128–4096) |

A plugin is one file: `<fs_homepath>/<mod folder>/lua/plugins/<name>/plugin.lua`,
a loose file (never inside a pk3), read as text (bytecode is refused).
`<name>` is letters, digits, `_` and `-`, up to 32 characters. The
shipped examples are `greeter`, `events` and `quest`: copy `lua/` from
`assets/server/` next to the mod's `database/` folder and set
`rp_lua_plugins "greeter events"` (`quest` needs its table edited for your
map first).

Stored data goes to `<mod folder>/database/lua.db`, beside `accounts.db`.

## Admin commands

`/scripts` needs the admin power **Scripts** (bit 29 in the calculator):

| Command | Does |
|---|---|
| `/scripts` or `/scripts list` | each plugin: running or off, memory, errors, timers, commands, watches |
| `/scripts stats [name]` | calls, time, messages sent and refused, deferred timers, stored bytes; props and NPCs alive, uses, effects, sounds |
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
| actions (`Msg.*`, `World.use/prop/npc`, `Effect.play`, `Sound.play`) | one bucket of 4, one token back per `sv_floodProtect` ms | `false, "too fast"` |
| props / NPCs alive | 32 / 8 | `false, "too many props"` / `"too many npcs"` |
| spawns per map | 128 props / 32 NPCs | `false, "spawn limit for this map"` |
| entity slots | the server's reserve of 64 kept free, as for `/entadd` | `false, "no room"` |
| messages to one player | 4 a second across all plugins | `false, "player busy"` |
| timers | 32 live, 50 ms minimum | `false, "too many timers"` |
| commands | 16, names of 3–24 characters | `false, "..."` |
| watched targetnames | 32 (256 for all plugins) | `false, "..."` |
| text | 900 bytes per message or log line | error |
| storage | keys 64 bytes, values 4 KB, 1 MB per plugin | `false, "storage full"` |

A `Msg.broadcast` costs `ceil(recipients / 8)` tokens; everything else
costs one. So a handler that plays an effect, uses a door and prints a line
spends three of the four, and the next action within the second is
refused: keep handlers short and check the results. Plugin commands cost
nothing: the player's own flood protection paces them.

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
| `entity_used` | `targetname`, `entity` (`number`, `classname`), `activator` (a player, or nil) — only for targetnames the plugin watches, and never for a use a script made |
| `npc_death` | `entity` (the handle), `npc_type`, `killer` (a player, or nil), `mod` — only for NPCs this plugin spawned |

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

### The world (P2)

```lua
World.use("door1", p)            -- every entity named door1, used as a button would with p as activator
World.prop("misc_model_breakable", {x=0,y=0,z=0}, {x=0,y=90,z=0}, {model="models/x.md3", health=50}, {name="crate"})
World.npc("jedi", {x=0,y=0,z=0}, {x=0,y=90,z=0}, {name="guard"})
World.remove(h)                  -- queued: the entity goes at the end of the frame
Effect.play("env/small_fire", origin, angles)
Sound.play("sound/x.wav", origin)
```

`World.use` goes through the game's own use path: the player's account
meets any lock on the entity (a script is not an admin), the stock chain
limits apply, and what the entity does to its activator is what it does
to anyone — a `target_teleporter` moves them, a `target_give` gives them
the map's items, a `target_kill` kills them. NPCs, vehicles, held entities
and entities with no use are skipped. It returns `used, skipped`.

`World.prop` spawns one of the Entity System's exempt classes — the ones
`/entedit` and `/entremove` may touch even on the map's own entities:
weapons, ammo, items, dispensers and racks, `misc_model_breakable`,
`misc_model_ghoul`, `misc_model_cargo_small`, `misc_exploding_crate`,
`misc_gas_tank`, `fx_runner`, `target_speaker`. The keys are the class's
own, as for `/entadd`, except the link keys (`targetname`,
`script_targetname`, `team`, `target`…`target6`), `rpLock` and the spawner
reward keys, which are refused. A script's entity is named
`lua.<plugin>.<name>` when `{name=...}` is given, so it can never take a map
entity's name. `/entsave` leaves script entities out; `/entremove`,
`/entedit` and `/entload` treat them as any other, and the handle goes
stale then. A dead NPC counts as alive until its body is gone or removed.

Handles: `h.number`, `h.classname`, `h.origin`, `h.targetname`,
`h.health`, `h.npc_type`; `h:valid()`. Two handles to the same entity are
equal. Unloading, reloading or disabling a plugin removes everything it
spawned, after its `shutdown` event.

`Effect.play` and `Sound.play` only play names already registered on the
map (by the map, the Entity System or `/playsound`); a script never
registers one.

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
`"count"`. Plugins cannot call each other, touch accounts, credits or
levels, kick, rename, damage, teleport or give to a player directly, spawn
anything outside the exempt classes, give NPCs orders, or change cvars.
That is by design for this stage.

## Writing a plugin

Start from `plugins/greeter/plugin.lua` (storage and events),
`plugins/events/plugin.lua` (timers, a command, the message bucket) or
`plugins/quest/plugin.lua` (an NPC, a prop, a watched button, a use as the
reward, `npc_death`). Keep
each handler short: the budget is 10 ms a call, and anything heavier
belongs in a timer that does a slice at a time. Check the result of
`Msg.*` and `Storage.*` calls when it matters, and look at
`/scripts stats` on a busy server to see what was refused.
