#!/bin/sh
# Runs the Lua test programs (build.sh first). Each runs in its own scratch folder.
cd "$(dirname "$0")"
rc=0
for t in t_core t_api t_store t_hostile; do
	mkdir -p out/run_$t && ( cd out/run_$t && ../$t ) || rc=1
done
exit $rc
