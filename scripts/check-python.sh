#!/usr/bin/env bash
# Builds the Python package into a venv and runs the Python tests.
# Usage: scripts/check-python.sh [venv]   (default: ./build-venv; created with uv
# or python3 -m venv if missing; needs numpy, zarr, numcodecs, pytest, pybind11 and
# scikit-build-core available to pip).
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
venv="${1:-build-venv}"
if [ ! -x "$venv/bin/python" ]; then
  if command -v uv >/dev/null; then uv venv "$venv"; else python3 -m venv "$venv"; fi
fi
# shellcheck disable=SC1091
source "$venv/bin/activate"
if command -v uv >/dev/null; then pip=(uv pip); reinstall=(--reinstall-package dp3core-dysco)
else pip=(python -m pip); reinstall=(--force-reinstall --no-deps); fi
"${pip[@]}" install numpy zarr numcodecs pytest crc32c "pybind11>=2.13" "scikit-build-core>=0.10" >/dev/null
"${pip[@]}" install --no-build-isolation "${reinstall[@]}" . >/dev/null
python -c "import dp3core_dysco, zarr, numcodecs; print('dp3core_dysco', dp3core_dysco.__version__, 'zarr', zarr.__version__, 'numcodecs', numcodecs.__version__)"
python -m pytest -q -W error python/tests
