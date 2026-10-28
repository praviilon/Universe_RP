#!/bin/sh
# Builds the client-side test programs against the real cgame/ui sources, with stub engines.
set -e
cd "$(dirname "$0")"
R=../..
B=${BUILD_DIR:-/home/claude/build}
SAN=${SAN--fsanitize=address,undefined}
CFLAGS="-g -O1 $SAN -fno-omit-frame-pointer -D_CGAME -DARCH_STRING=\"x86_64\" -I$R/codemp -I$R/shared -I$B/shared -I$B -w"
mkdir -p out
gcc $CFLAGS -o out/t_cgdlg t_cgdlg.c $R/codemp/cgame/cg_rpdialog.c $R/codemp/qcommon/q_shared.c $R/shared/qcommon/q_math.c $R/shared/qcommon/q_color.c $R/shared/qcommon/q_string.c -lm
echo built
