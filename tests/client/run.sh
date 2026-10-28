#!/bin/sh
cd "$(dirname "$0")"
rc=0
for t in t_cgdlg; do ( cd out && ./$t ) || rc=1; done
exit $rc
