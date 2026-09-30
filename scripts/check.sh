#!/usr/bin/env bash
# Builds and tests every available compiler configuration, with -Werror.
# Usage: scripts/check.sh [build-root]   (default: ./build-check)
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
root="${1:-build-check}"
configs=()
command -v g++ >/dev/null && configs+=("gcc-release:g++::Release" "gcc-debug:g++::Debug"
  "gcc-asan-ubsan:g++:-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer:Debug")
if command -v clang++ >/dev/null; then
  configs+=("clang-release:clang++::Release")
  # libc++ build only if the headers are installed.
  if echo '#include <vector>' | clang++ -stdlib=libc++ -x c++ -fsyntax-only - 2>/dev/null; then
    configs+=("clang-libcxx-release:clang++:-stdlib=libc++:Release")
  fi
fi
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
mkdir -p "$root"
status=0
for entry in "${configs[@]}"; do
  IFS=: read -r name cxx cxxflags type <<<"$entry"
  dir="$root/$name"
  echo "=== $name ($cxx $cxxflags, $type)"
  cmake -S . -B "$dir" "${generator[@]}" -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_CXX_FLAGS="$cxxflags" -DCMAKE_BUILD_TYPE="$type" \
    -DDP3CORE_DYSCO_WERROR=ON >"$dir.configure.log" 2>&1 || { cat "$dir.configure.log"; status=1; continue; }
  cmake --build "$dir" >"$dir.build.log" 2>&1 || { cat "$dir.build.log"; status=1; continue; }
  ctest --test-dir "$dir" --output-on-failure || status=1
done
exit $status
