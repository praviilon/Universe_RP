#!/bin/sh
# Builds the Lua test programs against the real rp_lua_*.c, lib/lua and sqlite, with stubs.c for the game.
set -e
cd "$(dirname "$0")"
R=../..
B=${BUILD_DIR:-/home/claude/build}
SAN=${SAN--fsanitize=address,undefined}	# SAN= for a plain build (valgrind)
CFLAGS="-g -O1 $SAN -fno-omit-frame-pointer -D_GAME -DARCH_STRING=\"x86_64\" -I$R/codemp -I$R/shared -I$R/lib/lua -I$B/shared -I$B -w"
SRC="$R/galaxyrp/game/rp_lua_core.c $R/galaxyrp/game/rp_lua_events.c $R/galaxyrp/game/rp_lua_api.c $R/galaxyrp/game/rp_lua_store.c $R/galaxyrp/game/rp_lua_admin.c $R/codemp/qcommon/q_shared.c $R/shared/qcommon/q_math.c $R/shared/qcommon/q_color.c $R/shared/qcommon/q_string.c stubs.c"
LUA="$R/lib/lua/*.c"
mkdir -p out
gcc -c -O1 -w -I$R/lib/lua -o out/sqlite3.o $R/shared/sqlite/sqlite3.c
for t in t_core t_api t_store t_hostile; do
	gcc $CFLAGS -o out/$t $t.c $SRC $LUA out/sqlite3.o -lm -ldl -lpthread
done
echo built
