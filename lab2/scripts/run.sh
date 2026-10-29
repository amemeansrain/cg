#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -x "$project_dir/build/cg_lab2" ]]; then
  "$project_dir/scripts/build.sh"
fi
cd "$project_dir"
exec "$project_dir/build/cg_lab2" "$@"
