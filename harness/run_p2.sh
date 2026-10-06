#!/bin/bash
# Mac entry point. Copies this repository into the dp3gate mount and runs
# harness/run_p2_inner.sh there.
#   DATASETS=synthetic BITS=10 harness/run_p2.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
CB=${CB:-/Users/joaquin/Dev/DP3Core/DP3Core-build}
mkdir -p "$CB/s12-codec" "$CB/bench/p2/logs"
rsync -a --delete --exclude .git --exclude 'build*' "$HERE/" "$CB/s12-codec/"
ENV_ARGS=()
[ -n "${DATASETS:-}" ] && ENV_ARGS+=(-e "DATASETS=${DATASETS}")
[ -n "${BITS:-}" ] && ENV_ARGS+=(-e "BITS=${BITS}")
[ -n "${MIN_FREE_GB:-}" ] && ENV_ARGS+=(-e "MIN_FREE_GB=${MIN_FREE_GB}")
docker exec "${ENV_ARGS[@]}" dp3gate bash /data/DP3Core-build/s12-codec/harness/run_p2_inner.sh
