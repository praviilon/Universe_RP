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
past. A step that copies or scans `n` bytes counts `1 + n/16` steps
(`GLXU_STEPS`): the replacement string `add_s()` copies for every match
(and a `%0` capture), a bracket class scanned per character (`[...]` in
`singlematch()`, twice in `%f[...]`), the needle `lmemfind()` compares, the
piece `str_rep()` repeats. At one step each, a 5000-match `gsub` with a
100 KB replacement and a 200 KB bracket class ran 10-30 times past the
budget before the check saw them. `str_rep()` also returns `""` at once
when there is nothing to repeat (`("")`:rep(n) looped n times for nothing).
These loops run no VM instruction, so the count hook alone cannot stop
them: a backtracking pattern (`('a'):rep(200000):find('.-.-.-.-x')`),
`%b()` over a long unbalanced string, a long linear pattern, a long plain
find.

**`ltablib.c`**, the lines marked `GLX_U`: `aux_getn()` refuses a length
past 2^24 ("table too big") before any loop runs - a `__len` metamethod
may return anything, and `table.remove(t, 1)` or `table.insert(t, 1, v)`
on a table whose `__len` says `math.maxinteger` is a C loop that never
ends, with no VM instruction for the hook to count. The library's own
loops call `RP_Lua_LoopCheck()` every 1024 steps: the shifts in
`tinsert()` and `tremove()`, `tmove()` (a huge range over an empty table),
`tconcat()` (each piece and separator weighted by its length, as in
`lstrlib.c`), and `table.sort`. The sort keeps one counter for the whole
call, passed down every level of its recursion (`sort()` → `auxsort()` →
`partition()` → `sort_comp()`), and counts every comparison whatever it
returns, a comparison of two strings without a function as `1 + the
shorter length / 16` (it is `strcoll` over them), and every swap round of
the partition. Before, each level kept its own counter and only a
comparison that returned true was counted, so equal keys - 8192 copies of
one 200 KB string - sorted for most of a second, uncounted, every frame.

Every other `.c` file is byte-identical to the release. When upgrading,
replace every file, then re-apply the `luaconf.h` blocks and the `lstrlib.c`
and `ltablib.c` lines (`grep GLX_U`).
