#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-$here/build}"
prefix="${PREFIX:-$HOME/.local}"
plugin_dir="lib/qt6/plugins"

if [ -d "$prefix/lib/plugins" ] && [ ! -d "$prefix/$plugin_dir" ]; then
  plugin_dir="lib/plugins"
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found; install CMake to build the SSH helper." >&2
  exit 1
fi

cmake -S "$here" -B "$build_dir" \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DKDE_INSTALL_PLUGINDIR="$plugin_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$build_dir"
cmake --install "$build_dir"

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

echo "Installed. Restart KRunner with: $restart_cmd"
