#!/bin/bash
# Mac entry. Copies this repository into the dp3gate mount and runs the
# Zenodo 1044019 benchmark there. Launch with the codec repository as the
# working directory, or call this file by path.
#   bash bench/zenodo/run.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")/../.." && pwd)
CB=${CB:-/Users/joaquin/Dev/DP3Core/DP3Core-build}
mkdir -p "$CB/s12-codec" "$CB/bench/zenodo-1044019/logs"
rsync -a --delete --exclude .git --exclude 'build*' "$HERE/" "$CB/s12-codec/"
ENV_ARGS=()
[ -n "${MIN_FREE_GB:-}" ] && ENV_ARGS+=(-e "MIN_FREE_GB=${MIN_FREE_GB}")
[ -n "${JOBS:-}" ] && ENV_ARGS+=(-e "JOBS=${JOBS}")
docker exec "${ENV_ARGS[@]}" dp3gate bash /data/DP3Core-build/s12-codec/bench/zenodo/inner.sh
