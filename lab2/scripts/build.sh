#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$project_dir/scripts/bootstrap_deps.sh"
if [[ -x "$project_dir/.deps/python/cmake/data/bin/cmake" ]]; then
  cmake_bin="$project_dir/.deps/python/cmake/data/bin/cmake"
else
  cmake_bin="$(command -v cmake)"
fi
"$cmake_bin" -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release "$@"
"$cmake_bin" --build "$project_dir/build" --parallel "${CG_BUILD_JOBS:-8}"
"$(dirname "$cmake_bin")/ctest" --test-dir "$project_dir/build" --output-on-failure
