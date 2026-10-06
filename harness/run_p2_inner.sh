#!/bin/bash
# Runs inside dp3gate. Builds the harness if needed, then runs it.
# DATASETS="synthetic" or "synthetic tc00". BITS="10" or "6 8 10 12 16".
set -euo pipefail
CB=${CB:-/data/DP3Core-build}
SRC=${SRC:-$CB/s12-codec}
BUILD=${BUILD:-$CB/s12-codec-build}
WORK=${WORK:-$CB/bench/p2/work}
REPORTS=${REPORTS:-$CB/bench/p2/reports}
LOG=${LOG:-$CB/bench/p2/logs}
TC00=${TC00:-$CB/e2e/TC00/input/TC00.MS}
MIN_FREE_GB=${MIN_FREE_GB:-10}
DATASETS=${DATASETS:-synthetic tc00}
BITS=${BITS:-6 8 10 12 16}
JOBS=${JOBS:-3}
mkdir -p "$WORK" "$REPORTS" "$LOG"
# shellcheck disable=SC1091
source "$CB/gatekit/container/gk-env.sh"
PY=${PY:-$CB/deps/pyvenv/bin/python}
DP3=${DP3:-/opt/hilda/bin/DP3}

free_gb() { df -BG "$WORK" | awk 'NR==2 {gsub(/G/,"",$4); print $4}'; }
need_space() {
  local free
  free=$(free_gb)
  if [ "$free" -lt "$MIN_FREE_GB" ]; then
    echo "stopping: ${free}GB free under $WORK, floor is ${MIN_FREE_GB}GB" >&2
    exit 3
  fi
}

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DDP3CORE_DYSCO_HARNESS=ON -DBUILD_TESTING=OFF \
    > "$LOG/cmake.log" 2>&1 || { cat "$LOG/cmake.log"; exit 1; }
fi
cmake --build "$BUILD" -j "$JOBS" --target dp3core_dysco_p2 dp3core_dysco_bench \
  > "$LOG/build.log" 2>&1 || { tail -80 "$LOG/build.log"; exit 1; }
HARNESS=$BUILD/harness/dp3core_dysco_p2

run_one() {
  local ds=$1 bits=$2 input=$3 caveat=$4
  local tag="${ds}-d${bits}"
  local dir=$WORK/$tag
  rm -rf "$dir"
  mkdir -p "$dir"
  local runs=()
  local i
  for i in 1 2 3 4 5; do
    need_space
    local out=$dir/run$i.MS
    echo "DP3 $tag run$i"
    "$DP3" msin="$input" msout="$out" msout.overwrite=true numthreads=1 steps=[] \
      msout.antennacompression=false msout.uvwcompression=false msout.scalarflags=false \
      msout.storagemanager=dysco msout.storagemanager.databitrate="$bits" \
      msout.storagemanager.weightbitrate=12 \
      msout.storagemanager.distribution=TruncatedGaussian \
      msout.storagemanager.disttruncation=2.5 \
      msout.storagemanager.normalization=AF \
      > "$LOG/dp3-${tag}-run${i}.log" 2>&1
    runs+=("$out")
  done
  local outdir=$REPORTS/$tag
  rm -rf "$outdir"
  set +e
  "$HARNESS" --input "$input" --casacore "${runs[@]}" --out "$outdir" --caveat "$caveat" \
    > "$LOG/p2-${tag}.log" 2>&1
  local rc=$?
  set -e
  echo "p2 ${tag}: exit ${rc}"
  rm -rf "$dir"
  if [ "$ds" = "synthetic" ] && [ "$rc" -ne 0 ]; then
    exit "$rc"
  fi
}

hash_tree() {
  local root=$1 dest=$2
  (cd "$root" && find . -type f -print0 | sort -z | xargs -0 sha256sum > "$dest")
}

for ds in $DATASETS; do
  if [ "$ds" = "synthetic" ]; then
    need_space
    if [ ! -d "$WORK/skeleton.MS" ]; then
      rm -rf "$WORK/skeleton.MS"
      "$DP3" msin="$TC00" msout="$WORK/skeleton.MS" msout.overwrite=true numthreads=1 steps=[] \
        msin.starttimeslot=0 msin.ntimes=1 > "$LOG/dp3-skeleton.log" 2>&1
    fi
    if [ ! -d "$WORK/synthetic.MS" ]; then
      "$PY" "$SRC/harness/make_synthetic_ms.py" "$WORK/skeleton.MS" "$WORK/synthetic.MS" \
        > "$LOG/synthetic.log" 2>&1 || {
        cat "$LOG/synthetic.log" >&2
        rm -rf "$WORK/synthetic.MS"
        exit 1
      }
    fi
    hash_tree "$WORK/synthetic.MS" "$LOG/sha256_before.txt"
    for b in $BITS; do
      run_one synthetic "$b" "$WORK/synthetic.MS" ""
    done
    hash_tree "$WORK/synthetic.MS" "$LOG/sha256_after.txt"
    if ! cmp -s "$LOG/sha256_before.txt" "$LOG/sha256_after.txt"; then
      echo "synthetic input changed" >&2
      exit 4
    fi
    echo "sha256 synthetic input unchanged"
  elif [ "$ds" = "tc00" ]; then
    need_space
    # SHA256SUMS lists TC00.MS/.DS_Store, which is not in this tree. Every other
    # listed file is checked. A mismatch still leaves a non-OK line in the log.
    (cd "$(dirname "$TC00")" && grep -v '/\.DS_Store$' SHA256SUMS | sha256sum -c > "$LOG/sha256_tc00_before.txt") || true
    caveat="TC00 DATA is already Dysco-10, so absolute errors are relative to quantized data."
    for b in $BITS; do
      run_one tc00 "$b" "$TC00" "$caveat" || true
    done
    (cd "$(dirname "$TC00")" && grep -v '/\.DS_Store$' SHA256SUMS | sha256sum -c > "$LOG/sha256_tc00_after.txt") || true
    echo "sha256_tc00_before non-OK: $(grep -vc ': OK$' "$LOG/sha256_tc00_before.txt" || true)"
    echo "sha256_tc00_after non-OK: $(grep -vc ': OK$' "$LOG/sha256_tc00_after.txt" || true)"
  else
    echo "unknown dataset $ds" >&2
    exit 2
  fi
done
