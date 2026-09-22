#!/usr/bin/env bash
# Build a Release minidb_bench and print one point-lookup measurement.
# Does not overwrite bench/results.md. Copy the output there when recording a run.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-"$root/build-bench"}"

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$build_dir" --target minidb_bench --parallel

echo "machine: $(uname -srm)"
if command -v c++ >/dev/null 2>&1; then
  echo "compiler: $(c++ --version | head -n 1)"
fi
if [[ -r /proc/cpuinfo ]]; then
  awk -F: '/model name/ { gsub(/^ +/, "", $2); print "cpu: " $2; exit }' /proc/cpuinfo
fi
echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
"$build_dir/minidb_bench"
