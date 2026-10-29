# Lua 5.4.9 (vendored)

The `src/` of the official `lua-5.4.9.tar.gz` from lua.org (2026-08-10,
SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`),
identical to the GitHub `v5.4.9` tag. MIT licence: see `LICENSE`.

Compiled into the server game module (jampgame) only, for the mod's
server-side scripting. See `galaxyrp/game/rp_lua*.c` and
`assets/server/lua/README.md` once they exist.

## Left out, on purpose

These files are not in the tree, so the libraries they provide cannot be
opened by any script, whatever a later bug does:

| File | Why |
|---|---|
| `liolib.c` | the `io` library: files |
| `loslib.c` | the `os` library: `os.execute`, `os.remove`, `os.exit`... |
| `loadlib.c` | the `package` library: loading native modules |
| `ldblib.c` | the `debug` library: hooks and raw access the sandbox relies on |
| `linit.c` | `luaL_openlibs`, which opens all of the above; the game opens its own list |
| `lua.c`, `luac.c` | the stand-alone interpreter and compiler |

## Changed from the release

**`luaconf.h`** (the file Lua's authors mean for configuration), three
limits, each in a block marked `GLX_U`:

| Define | Release | Here | Why |
|---|---|---|---|
| `LUAI_MAXSTACK` | 1,000,000 | 20,000 | one instruction can copy the whole stack (`{...}`, `table.pack`, `coroutine.wrap`); 20,000 slots is milliseconds, a million is seconds |
| `LUAI_MAXCCALLS` | 200 | 100 | C recursion depth (nested pcalls, metamethods, pattern matching) |
| `LUA_MAXCAPTURES` | 32 | 16 | captures per pattern |

**`lstrlib.c`**, the lines marked `GLX_U`: `MatchState` counts its match
steps across a whole call (`prepstate`), and every 1024 of them, in
`match()` at its `init:` label (so the tail loop counts too), in
`matchbalance()`, in `max_expand()`, in the plain-find `lmemfind()` and in
`str_rep()`'s copy loop, the library calls `RP_Lua_LoopCheck()`
(`rp_lua_core.c`), which raises "ran too long" once the call's deadline is
past. `str_rep()` also returns `""` at once when there is nothing to repeat
(`("")`:rep(n) looped n times for nothing). These loops run no VM
instruction, so the count hook alone cannot stop them: a backtracking
pattern (`('a'):rep(200000):find('.-.-.-.-x')`), `%b()` over a long
unbalanced string, a long linear pattern, a long plain find.

**`ltablib.c`**, two lines marked `GLX_U`: `tmove()` (`table.move`) calls
`RP_Lua_LoopCheck()` every 1024 elements, for the same reason (a huge range
over an empty table is a pure C loop).

Every other `.c` file is byte-identical to the release. When upgrading,
replace every file, then re-apply the `luaconf.h` blocks and the `lstrlib.c`
and `ltablib.c` lines (`grep GLX_U`).
