# Server-side Lua plugins (P1 + P2)

Universe RP's server can run small Lua 5.4 scripts called plugins. A plugin
is told what happens on the server and can answer with messages, chat
commands, timers and stored data (P1), and act on the world the way a
player would (P2): use a named entity as a button would, place props,
NPCs and vehicles of its own, play effects and sounds the map already has. It never
reaches past that: no accounts, no kicking, renaming or damaging anyone, no
giving or taking from a player directly — what a script wants done to a
player, the map's own mechanics do (a `target_give`, a `target_teleporter`,
a `trigger_hurt`, a `target_reward`, a `target_dialog`), with that player as
activator. How much a plugin pays and how often is the server host's design;
that the server stays up and nobody is dropped, whatever a plugin does, is
the budget's. A plugin that misbehaves is cut off by its own budget, never
the server.

## Setup

| Cvar | Default | Meaning |
|---|---|---|
| `rp_lua` | `1` | `0` turns the whole layer off (latched: takes effect at the next map) |
| `rp_lua_plugins` | `""` | plugin folder names to load at map start, separated by spaces or commas |
| `rp_lua_memkb` | `512` | memory per plugin in KB (128–4096); applies when a plugin is loaded or reloaded, never to a running one |

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
| `/scripts stats [name]` | calls, time, messages sent and refused, deferred timers, stored bytes; props, NPCs and vehicles alive, uses, effects, sounds; what the guards held back |
| `/scripts help` | every command the plugins add, with its help text |
| `/scripts load <name>` | load a plugin (for this map; `rp_lua_plugins` is not changed) |
| `/scripts unload <name>` | stop it and free its slot |
| `/scripts reload <name>` or `all` | restart with a fresh state (re-reads the file) |
| `/scripts disable <name>` | stop it, keep the slot (what 20 errors in a minute does) |
| `/scripts enable <name>` | start it again with a fresh error count |

Loads, unloads and reloads happen at the end of the current server frame,
never inside a running script.

A map, an entity file or `/entadd` can do the same with a **`target_lua`**
entity (`plugin <name>`, `action load|unload|reload`, spawnflag 1 ONCE): a
button, a trigger, a dialogue option or `/entuse` that uses it loads the
plugin as `/scripts load` would, through the same checks, at the end of the
frame. A quest plugin a map button starts, for example. A script cannot set
one off: `World.use` skips it, and a chain a script started is refused when
it reaches it. See `/enthelp target_lua`.

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
| one call (an event, a command, a timer) | 10 ms, 1,000,000 instructions | the call ends with "ran too long" (a `pcall` cannot catch it: every instruction after it raises again until the call is over) |
| all calls in one frame | 20 ms (50 ms for all plugins together) | timers wait for the next frame; events and commands still run, with a 1 ms deadline; past twice the budget they are not run (`init` and `shutdown` always are) |
| memory | `rp_lua_memkb` | "not enough memory" in the call |
| errors | 20 in 60 s | the plugin is switched off (`/scripts enable` brings it back) |
| actions (`Msg.*`, `World.use/prop/npc/vehicle`, `Effect.play`, `Sound.play`) | one bucket of 4, one token back every 150 ms (`sv_floodProtect`, fixed by the game) | `false, "too fast"` |
| props / NPCs / vehicles alive | 32 / 8 / 6 | `false, "too many props"` / `"too many npcs"` / `"too many vehicles"` |
| spawns per map | 128 props (16 of them dialogues) / 32 NPCs / 24 vehicles | `false, "spawn limit for this map"` |
| script vehicles on the server | 24, all plugins and released ones together | `false, "too many vehicles on the server"` |
| entity slots | the server's reserve of 64 kept free, as for `/entadd` | `false, "no room"` |
| messages and `World.use`s to one player | 4 a second across all plugins | `false, "player busy"` |
| commands a script call sets off, to one client (what a `World.use`'s chain prints, a dialogue, `Msg.*`) | 32 a second across all plugins | not sent; one log line a second |
| a music change a script call sets off (`target_play_music`: one command to everyone) | needs room in every client's 32 | the music is not changed; one log line |
| rewards a script call sets off (each is a database write) | 4 a server frame across all plugins | not paid, and the reward does not fire its target; one log line a frame |
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

The last two rows only count what happens inside a script call. A player
pressing a button, answering a dialogue or killing an NPC is never counted,
and neither is what fires later through a `target_delay` (it runs as the
map's own action).

An event raised by what a script call itself did is not delivered: a
`World.use` that pays a level up (`level_up`), replaces a player's open
dialogue (`dialog_answer` with reason `replaced`) or kills a player through
a map entity (`player_death`) raises nothing in any plugin, and the log
says so. A plugin is only ever charged for its own code.

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
| `dialog_answer` | `player`, `dialog` (the `target_dialog`'s targetname), `entity` (`number`, `classname`), `choice` (the number the player chose, or nil), `option` (its text, or nil), `reason` (`answered`, or `cancelled`, `timeout`, `away`, `died`, `left`, `replaced` (another dialogue opened for the player; the same one used again only refreshes its timer), `gone`) — told after the option did its work; nothing a script does changes it |

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
World.npc("jedi", {x=0,y=0,z=0}, {x=0,y=90,z=0}, {name="guard", credits=500, xp=20})
World.vehicle("swoop", {x=0,y=0,z=0}, {x=0,y=90,z=0}, {name="bike1", team="player", scale=120})
World.remove(h)                  -- queued: the entity goes at the end of the frame
Effect.play("env/small_fire", origin, angles)
Sound.play("sound/x.wav", origin)
```

`World.use` goes through the game's own use path: the player's account
meets any lock on the entity (a script is not an admin), the stock chain
limits apply, and what the entity does to its activator is what it does
to anyone — a `target_teleporter` moves them, a `target_give` gives them
the map's items, a `target_kill` kills them. NPCs, vehicles, held entities
and entities with no use are skipped. It returns `used, skipped`. A chain
may reach a `target_reward` (it pays the player, as a button would: its own
`wait`, `playerwait`, ONCE and ONCE_EACH apply, and the log line names the
plugin) or a `target_dialog` (it opens for the player; the answer is the
player's own `/dialog`, so whatever the option fires runs as their action).
Each `World.use` takes one of the player's four a second (`"player busy"`).

`World.prop` spawns one of the Entity System's exempt classes — the ones
`/entedit` and `/entremove` may touch even on the map's own entities:
weapons, ammo, items, dispensers and racks, `misc_model_breakable`,
`misc_model_ghoul`, `misc_model_cargo_small`, `misc_exploding_crate`,
`misc_gas_tank`, `fx_runner`, `target_speaker`, and the quest entities
`target_dialog`, `target_reward`, `target_flag`, `target_play_music`,
`target_teleporter`, `target_position` and `target_level_change` (see
`/enthelp` for their keys). The keys are the class's own, as for
`/entadd`, except `targetname`, `script_targetname`, `team`, `lock` and
the spawner reward keys, which are refused. A script's entity is named
`lua.<plugin>.<name>` when `{name=...}` is given, so it can never take a map
entity's name; admins reach it by that name (`/npc kill lua.quest.guard`,
`/entuse`).

**A script's entities link only to each other.** Every link key — `target`
to `target6`, `paintarget`, `opentarget`, `closetarget`, `npc_target`, and a
dialogue's `go1`…`go6` and `speaker` — takes the plain name of one of the
plugin's own entities and is stored as `lua.<plugin>.<name>`:
`go1 = "pay"` fires the plugin's `pay`. So a plugin's dialogue can pay its
own reward, set its own flags, play its own music or open its next
dialogue, but never a map door, a map `target_kill` or another plugin's
entity. (Flag names, `flag` and `need<n>`, are per-player state, not links,
and stay as given.) A dialogue's options fire when the player answers, as
their own action. A plugin may spawn 16 dialogues per map (each keeps its
text in the level's memory until the map ends); the other classes count
within the 128 props.

```lua
local pay  = World.prop("target_reward", o, nil, {credits=50, xp=5, message="Quest done"}, {name="pay"})
local done = World.prop("target_flag",   o, nil, {flag="quest_done", target="pay"}, {name="done"})
local talk = World.prop("target_dialog", o, nil, {message="Did you do it?", option1="Yes", need1="quest_done", go1="done",
                                                   option2="Not yet"}, {name="talk"})
World.use("lua.quest.talk", p)           -- opens the dialogue for p; "Yes" sets the flag and pays, as p's own action
``` `/entsave` leaves script entities out; `/entremove`,
`/entedit` and `/entload` treat them as any other, and the handle goes
stale then. A dead NPC counts as alive until its body is gone or removed.

`World.npc` and `World.vehicle` take the options an admin's spawner takes,
set when the NPC or vehicle spawns (not changed afterwards: admins keep
`/npc team`, `/npc effect` and `/npc freeze` on it, as on any NPC):

| Option | Takes | Spawner key it matches | Vehicle |
|---|---|---|---|
| `name` | letters, digits, `_` | targetname `lua.<plugin>.<name>` | yes |
| `credits`, `xp` | 0–100000, 0–100: paid to the player who kills it | `npccredits`, `npcxp` | no |
| `health` | 1–100000: its maximum and starting health | `health` | no (the vehicle file's armor) |
| `team` | `player`, `enemy`, `neutral`, `free` | `npcteam` (`/npc team`) | yes |
| `effect` | `holo`, `ghost`, `walkthrough` | `npceffect` (`/npc effect`) | no |
| `frozen` | `true`: a statue that can still be hurt | `npcfreeze` (`/npc freeze`) | no |
| `noai` | `true`: stands and picks no fights, but still blocks and dodges | spawnflag CINEMATIC | no |
| `scale` | 20–500 percent | `npcscale` | yes |
| `hat`, `cape` | a name from the cosmetic lists, or `none` | `npchat`, `npccape` | no |

A wrong value, or an option a vehicle does not take, is an error and nothing
spawns. An admin kills a script's NPC by its name with
`/npc kill lua.<plugin>.<name>`.

`World.vehicle` places a vehicle of any type (speeders, swoops, animals,
fighters, walkers) as a map's `NPC_Vehicle` spawner would: it stands still
until a player boards it, and players ride it as any other. The vehicle file
decides the rest (its droid, whether it dies without a pilot). A type that is
not a vehicle is refused (`"not a vehicle type"`); `World.npc` refuses vehicle
types. A script never boards, drives or empties a vehicle: `World.use` skips
vehicles, and `World.remove` of one somebody rides is refused with
`false, "someone is riding it"`.

Handles: `h.number`, `h.classname`, `h.origin`, `h.targetname`,
`h.health`, `h.npc_type`; for an NPC or vehicle `h.max_health`, `h.team`,
`h.scale` (100 when none), `h.effect` (or nil), `h.frozen`, and for an NPC
`h.credits` and `h.xp` (what killing it pays, 0 once paid); for a vehicle also `h.vehicle_type` (`walker`,
`fighter`, `speeder`, `animal`, `flier`), `h.pilot` (the player riding it,
or nil) and `h.passengers` (how many ride behind the pilot); `h:valid()`.
Two handles to the same entity are equal. Unloading, reloading or disabling
a plugin removes everything it spawned, after its `shutdown` event — except
a vehicle someone is riding: the plugin lets it go (its handles go stale, a
reloaded plugin does not get it back), the ride goes on, and the vehicle
is removed once nobody is aboard. Those count toward the 24 on the server
until then.

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
`tonumber`), kept byte for byte: any string, `string.pack` output
included. Keys are text, 1 to 64 bytes, without a zero byte. Writes are kept in memory and written every 30 s and at map
end. Rows of a deleted character are removed with it.

### What is not there

No `io`, `os`, `package`, `debug` or `coroutine`; no `require`, `dofile`
or `loadfile`; `load` takes text only; `collectgarbage` only answers
`"count"`; `setmetatable` refuses a metatable with `__gc` (a finalizer would
run outside any call and its budget); `xpcall` calls its handler after the
error unwound, not at the point it was raised. Plugins cannot call each other, touch accounts, kick, rename,
damage, teleport, pay or give to a player directly (only through the map's
entities and their own NPCs' kill rewards), spawn anything outside the
exempt classes, link their entities to the map's, give NPCs orders, or
change cvars. That is by design for this stage.

## Writing a plugin

Start from `plugins/greeter/plugin.lua` (storage and events),
`plugins/events/plugin.lua` (timers, a command, the message bucket) or
`plugins/quest/plugin.lua` (an NPC with a kill reward, a prop, a watched
button, a use as the reward, `npc_death`, a vehicle). Keep
each handler short: the budget is 10 ms a call, and anything heavier
belongs in a timer that does a slice at a time. Check the result of
`Msg.*` and `Storage.*` calls when it matters, and look at
`/scripts stats` on a busy server to see what was refused.
