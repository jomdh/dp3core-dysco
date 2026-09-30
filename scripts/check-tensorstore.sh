#!/usr/bin/env bash
# Builds the TensorStore registration (design 7.3 option C), runs the C++
# TensorStore round-trip test and the zarr-python <-> TensorStore interop test.
# Usage: scripts/check-tensorstore.sh [build-dir] [venv]
#   build-dir defaults to build-tensorstore; venv defaults to build-venv (set up
#   by scripts/check-python.sh if missing).
# Environment:
#   DP3CORE_DYSCO_TENSORSTORE_URL  TensorStore source archive (default: pinned
#                                  GitHub v0.1.85 tarball; file:// URLs work)
#   FETCHCONTENT_BASE_DIR          where TensorStore's dependencies are fetched
#                                  (reuse it between builds; ~1.2 GB)
#   JOBS                           parallel build jobs (default: nproc)
# Needs CMake >= 3.24, Ninja, a C++17 compiler, nasm (for libjpeg-turbo/aom via
# TensorStore's CMake build) and network access unless everything is cached.
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
build="${1:-build-tensorstore}"
case "$build" in /*) ;; *) build="$(pwd)/$build" ;; esac
venv="${2:-build-venv}"
args=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDP3CORE_DYSCO_TENSORSTORE=ON)
[ -n "${DP3CORE_DYSCO_TENSORSTORE_URL:-}" ] && args+=("-DDP3CORE_DYSCO_TENSORSTORE_URL=$DP3CORE_DYSCO_TENSORSTORE_URL")
[ -n "${FETCHCONTENT_BASE_DIR:-}" ] && args+=("-DFETCHCONTENT_BASE_DIR=$FETCHCONTENT_BASE_DIR")
cmake -S . -B "$build" "${args[@]}"
cmake --build "$build" -j "${JOBS:-$(nproc)}" --target test_tensorstore_codec dp3core_dysco_ts_copy
"$build/tensorstore/test_tensorstore_codec"
if [ ! -x "$venv/bin/python" ]; then scripts/check-python.sh "$venv"; fi
# shellcheck disable=SC1091
source "$venv/bin/activate"
DP3CORE_DYSCO_TS_COPY="$build/tensorstore/dp3core_dysco_ts_copy" \
  python -m pytest -q -W error python/tests/test_tensorstore_interop.py
