#!/bin/bash
# Zenodo 1044019 benchmark inside dp3gate.
# One compressed MeasurementSet at a time. Stops below MIN_FREE_GB.
# The input tarball is only read.
set -euo pipefail
MIN_FREE_GB=${MIN_FREE_GB:-40}
JOBS=${JOBS:-3}
SRC=/data/DP3Core-build/s12-codec
BUILD=/data/DP3Core-build/s12-codec-build
BENCH_SRC=/data/DP3Core-build/bench/compression/src
TAR=/data/DP3Core-build/data/zenodo-1044019/RX42_SB100-109.2ch10s.ms.tar.gz
MD5=821afa9aff47a186e5341f21e16f7cdf
OUT=/data/DP3Core-build/bench/zenodo-1044019
WORK=$OUT/work
LOG=$OUT/logs
ROWS=$OUT/rows.json
PY=/opt/hilda/venv/bin/python3
DP3=/opt/hilda/bin/DP3
SCRIPT=$SRC/bench/zenodo/bench_dp3core.py
export BENCH_SRC
mkdir -p "$WORK" "$LOG"

free_gb() {
  df -BG "$WORK" | awk 'NR==2 {gsub(/G/, "", $4); print $4}'
}
need_space() {
  local free
  free=$(free_gb)
  echo "free_gb ${free}"
  if [ "$free" -lt "$MIN_FREE_GB" ]; then
    echo "stop: free ${free}G is below MIN_FREE_GB ${MIN_FREE_GB}"
    exit 3
  fi
}
have_row() {
  "$PY" - "$ROWS" "$1" "$2" <<'PY'
import json, os, sys
path, name, column = sys.argv[1:]
if not os.path.exists(path):
    sys.exit(1)
rows = json.load(open(path))
sys.exit(0 if any(r.get("name") == name and r.get("column") == column for r in rows) else 1)
PY
}
check_tar() {
  local tag=$1
  (cd "$(dirname "$TAR")" && echo "${MD5}  $(basename "$TAR")" | md5sum -c) | tee "$LOG/md5_${tag}.txt"
  sha256sum "$TAR" | tee "$LOG/sha256_tar_${tag}.txt"
}

echo "== zenodo inner $(date -u +%Y-%m-%dT%H:%M:%SZ)"
need_space
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo -n "DP3: "
  "$DP3" --version 2>&1 | tr '\n' ' '
  echo
  gcc_line=$(g++ --version)
  echo "${gcc_line%%$'\n'*}"
  uname -a
  "$PY" -c 'import numpy,sys; print("python:", sys.version.split()[0], "numpy", numpy.__version__)'
  free -m
} | tee "$LOG/env.txt"

echo "== md5 before"
check_tar before
[ -f "$TAR" ]

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DDP3CORE_DYSCO_HARNESS=ON -DBUILD_TESTING=OFF \
    > "$LOG/cmake.log" 2>&1 || { cat "$LOG/cmake.log"; exit 1; }
fi
cmake --build "$BUILD" -j "$JOBS" --target dp3core_dysco_bench \
  > "$LOG/build.log" 2>&1 || { tail -40 "$LOG/build.log"; exit 1; }
BIN=$BUILD/harness/dp3core_dysco_bench
[ -x "$BIN" ]

H=$(find /opt/hilda /usr/include /usr/local/include -path '*casacore/tables/Tables/Table.h' -print -quit)
INC=${H%/casacore/tables/Tables/Table.h}
LIBF=$(find /opt/hilda /usr/lib64 /usr/lib /usr/local/lib -name 'libcasa_tables.so' -print -quit)
LIBD=$(dirname "$LIBF")
g++ -O2 -std=c++17 -I"$INC" "$BENCH_SRC/msextract.cc" -o "$WORK/msextract" \
  -L"$LIBD" -Wl,-rpath,"$LIBD" -lcasa_tables -lcasa_casa \
  > "$LOG/build_msextract.log" 2>&1 || { cat "$LOG/build_msextract.log"; exit 1; }
X=$WORK/msextract

all_rows_present() {
  have_row "casacore plain" DATA || return 1
  local b
  for b in 6 8 10 12 16; do
    have_row "casacore DyscoStMan ${b}-bit" DATA || return 1
    have_row "dp3core.dysco ${b}-bit" DATA || return 1
  done
  have_row "casacore dysco_d0_w12" WEIGHT_SPECTRUM || return 1
  have_row "dp3core.dysco weights 12-bit" WEIGHT_SPECTRUM || return 1
}

if ! all_rows_present; then
echo "== encodes"
INPUT_FILE=$WORK/input_ms.path
if [ -s "$INPUT_FILE" ] && [ -d "$(cat "$INPUT_FILE")" ]; then
  INPUT_MS=$(cat "$INPUT_FILE")
  echo "input MS reused $INPUT_MS"
else
  need_space
  echo "== extract"
  rm -rf "$WORK/extract"
  mkdir -p "$WORK/extract"
  tar -xzf "$TAR" -C "$WORK/extract"
  INPUT_MS=$(find "$WORK/extract" -type d \( -name '*.ms' -o -name '*.MS' \) -print -quit)
  [ -n "$INPUT_MS" ] && [ -f "$INPUT_MS/table.dat" ]
  echo "$INPUT_MS" > "$INPUT_FILE"
  echo "input MS $INPUT_MS"
fi

COMMON=(msout.overwrite=true numthreads=1 steps=[]
  msout.antennacompression=false msout.uvwcompression=false msout.scalarflags=false)
DYSCO=(msout.storagemanager=dysco
  msout.storagemanager.weightbitrate=12
  msout.storagemanager.distribution=TruncatedGaussian
  msout.storagemanager.disttruncation=2.5
  msout.storagemanager.normalization=AF)

dp3_copy() {
  local name=$1
  shift
  local out=$WORK/ms_${name}.MS
  local t0 rc
  rm -rf "$out"
  t0=$(date +%s.%N)
  rc=0
  "$DP3" msin="$INPUT_MS" msout="$out" "${COMMON[@]}" "$@" > "$LOG/dp3_${name}.log" 2>&1 || rc=$?
  DP3_ENC=$(awk -v a="$t0" -v b="$(date +%s.%N)" 'BEGIN{printf "%.4f", b-a}')
  echo "DP3 ${name} rc=${rc} enc=${DP3_ENC}s"
  if [ "$rc" -ne 0 ]; then
    tail -20 "$LOG/dp3_${name}.log"
    exit 1
  fi
  DP3_MS=$out
}

extract_dump() {
  local ms=$1 dump=$2 what=$3
  rm -rf "$dump"
  mkdir -p "$dump"
  "$X" "$ms" "$dump" "$what" > "$LOG/extract_$(basename "$dump").log" 2>&1 || {
    cat "$LOG/extract_$(basename "$dump").log"
    exit 1
  }
}

if have_row "casacore plain" DATA && [ -f "$WORK/dump_plain/data.c64" ] && [ -f "$WORK/dump_plain/flag.u8" ]; then
  echo "skip plain"
else
  need_space
  dp3_copy plain
  extract_dump "$DP3_MS" "$WORK/dump_plain" meta,data,ws,flag
  "$PY" "$SCRIPT" sizes --ms "$DP3_MS" --dminfo "$WORK/dump_plain/dminfo.txt" --out "$WORK/sizes_plain.json"
  "$PY" "$SCRIPT" sigma --orig "$WORK/dump_plain" --work "$WORK"
  "$PY" "$SCRIPT" casacore --orig "$WORK/dump_plain" --work "$WORK" \
    --dump "$WORK/dump_plain" --sizes "$WORK/sizes_plain.json" --rows "$ROWS" \
    --name "casacore plain" --encode-s "$DP3_ENC" --plain
  rm -rf "$DP3_MS"
fi

if [ ! -f "$WORK/sigma_bl_pol.npy" ] || [ ! -f "$WORK/dataset.json" ]; then
  "$PY" "$SCRIPT" sigma --orig "$WORK/dump_plain" --work "$WORK"
fi

run_dysco_data() {
  local bits=$1
  local name="casacore DyscoStMan ${bits}-bit"
  if have_row "$name" DATA; then
    echo "skip $name"
    return 0
  fi
  need_space
  dp3_copy "d${bits}" "${DYSCO[@]}" "msout.storagemanager.databitrate=${bits}"
  extract_dump "$DP3_MS" "$WORK/dump_dysco" meta,data,ws
  "$PY" "$SCRIPT" sizes --ms "$DP3_MS" --dminfo "$WORK/dump_dysco/dminfo.txt" --out "$WORK/sizes_dysco.json"
  "$PY" "$SCRIPT" casacore --orig "$WORK/dump_plain" --work "$WORK" \
    --dump "$WORK/dump_dysco" --sizes "$WORK/sizes_dysco.json" --rows "$ROWS" \
    --name "$name" --encode-s "$DP3_ENC"
  rm -rf "$DP3_MS" "$WORK/dump_dysco"
}

for bits in 6 8 10 12 16; do
  run_dysco_data "$bits"
done

if have_row "casacore dysco_d0_w12" WEIGHT_SPECTRUM; then
  echo "skip dysco_d0_w12"
else
  need_space
  dp3_copy d0 "${DYSCO[@]}" msout.storagemanager.databitrate=0
  extract_dump "$DP3_MS" "$WORK/dump_dysco" meta,data,ws
  "$PY" "$SCRIPT" sizes --ms "$DP3_MS" --dminfo "$WORK/dump_dysco/dminfo.txt" --out "$WORK/sizes_dysco.json"
  "$PY" "$SCRIPT" casacore --orig "$WORK/dump_plain" --work "$WORK" \
    --dump "$WORK/dump_dysco" --sizes "$WORK/sizes_dysco.json" --rows "$ROWS" \
    --name "casacore dysco_d0_w12" --encode-s "$DP3_ENC" --weight-only
  rm -rf "$DP3_MS" "$WORK/dump_dysco"
fi

# The codec reads the plain dump. The extracted MeasurementSet is no longer needed.
rm -rf "$WORK/extract"
rm -f "$INPUT_FILE"

run_ours() {
  local kind=$1 bits=$2 name=$3
  local column=DATA
  [ "$kind" = "weights" ] && column=WEIGHT_SPECTRUM
  if have_row "$name" "$column"; then
    echo "skip $name"
    return 0
  fi
  need_space
  "$PY" "$SCRIPT" ours --orig "$WORK/dump_plain" --work "$WORK" --rows "$ROWS" \
    --name "$name" --bin "$BIN" --bits "$bits" --kind "$kind"
}

for bits in 6 8 10 12 16; do
  run_ours data "$bits" "dp3core.dysco ${bits}-bit"
done
run_ours weights 12 "dp3core.dysco weights 12-bit"
else
  echo "all rows present"
fi

"$PY" "$SCRIPT" finish --work "$WORK" --rows "$ROWS" --env "$LOG/env.txt" \
  --cmake "$SRC/CMakeLists.txt" --out "$OUT/doc.json"
"$PY" "$SRC/bench/zenodo/make_rows.py" "$OUT/doc.json" --out "$OUT/rows.md" | tee "$LOG/rows.md"

echo "== md5 after"
check_tar after
before=$(awk '{print $1}' "$LOG/sha256_tar_before.txt")
after=$(awk '{print $1}' "$LOG/sha256_tar_after.txt")
if [ "$before" != "$after" ]; then
  echo "tarball sha256 changed"
  exit 1
fi
echo "sha256 tarball unchanged"
rm -rf "$WORK/dump_plain" "$WORK"/ms_*.MS "$WORK"/dump_dysco "$WORK"/decoded-*.bin
echo "== zenodo inner end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
