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

**Only `luaconf.h`** (the file Lua's authors mean for configuration), three
limits, each in a block marked `GLX_U`:

| Define | Release | Here | Why |
|---|---|---|---|
| `LUAI_MAXSTACK` | 1,000,000 | 20,000 | one instruction can copy the whole stack (`{...}`, `table.pack`, `coroutine.wrap`); 20,000 slots is milliseconds, a million is seconds |
| `LUAI_MAXCCALLS` | 200 | 100 | C recursion depth (nested pcalls, metamethods, pattern matching) |
| `LUA_MAXCAPTURES` | 32 | 16 | captures per pattern |

Every `.c` file is byte-identical to the release. When upgrading, replace
every file, then re-apply the three `luaconf.h` blocks.
