#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-$here/build}"
manifest="$build_dir/install_manifest.txt"

if [ ! -f "$manifest" ]; then
  echo "install_manifest.txt not found in $build_dir." >&2
  echo "Run ./install.sh first or set BUILD_DIR to the build directory used for install." >&2
  exit 1
fi

while IFS= read -r installed_path; do
  if [ -n "$installed_path" ]; then
    rm -f "$installed_path"
  fi
done < "$manifest"

restart_cmd="kquitapp6 krunner && krunner"
if ! command -v kquitapp6 >/dev/null 2>&1; then
  if command -v kquitapp5 >/dev/null 2>&1; then
    restart_cmd="kquitapp5 krunner && krunner"
  elif command -v kquitapp >/dev/null 2>&1; then
    restart_cmd="kquitapp krunner && krunner"
  else
    restart_cmd="log out/in or restart plasma-krunner"
  fi
fi

echo "Uninstalled. Restart KRunner with: $restart_cmd"
