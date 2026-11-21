#!/usr/bin/env bash
# Undo everything install.sh wrote, using the .gptk-bak copies it left behind.
#   ./uninstall.sh [--check] [--dry-run]
set -euo pipefail

APPID=3240220
GAMENAME="Grand Theft Auto V Enhanced"
STEAM_SUPPORT="${STEAM_SUPPORT:-$HOME/Library/Application Support/Steam}"
BP_SUPPORT="${BP_SUPPORT:-$HOME/Library/Application Support/bridgeplay}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE=install

for arg in "$@"; do
  case "$arg" in
    --check) MODE=check ;;
    --dry-run) [ "$MODE" = install ] && MODE=dry ;;
    -h|--help) sed -n '2,5p' "$0"; exit 0 ;;
    *) printf 'unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

log() { printf '%s\n' "$*"; }
act() { case "$MODE" in check) printf '  would  %s\n' "$*" ;; dry) printf '  dry    %s\n' "$*" ;; *) "$@" ;; esac; }

restore() {
  local f="$1"
  if [ -e "$f.gptk-bak" ]; then act mv -f "$f.gptk-bak" "$f"; log "  restored $f"; return 0; fi
  if [ -e "$f" ]; then act rm -rf "$f"; log "  removed  $f"; fi
}

LIB=""
CANDIDATES=("$STEAM_SUPPORT")
[ -f "$STEAM_SUPPORT/steamapps/libraryfolders.vdf" ] && while IFS= read -r p; do CANDIDATES+=("$p"); done \
  < <(sed -n 's/.*"path"[[:space:]]*"\(.*\)".*/\1/p' "$STEAM_SUPPORT/steamapps/libraryfolders.vdf")
for c in "${CANDIDATES[@]}"; do [ -d "$c/steamapps/common/$GAMENAME" ] && LIB="$c" && break; done
[ -n "$LIB" ] || { echo "game not installed" >&2; exit 1; }

GAME="$LIB/steamapps/common/$GAMENAME"
PFX="$LIB/steamapps/compatdata/$APPID/pfx"
SYS="$PFX/drive_c/windows/system32"

RUNNER=""
for c in "$BP_SUPPORT"/runners/*/CrossOver; do
  [ -d "$c/lib64/apple_gptk" ] && RUNNER="$c"
done
ENGINE_W=""; ENGINE_U=""
[ -n "$RUNNER" ] && ENGINE_W="$RUNNER/lib/wine/x86_64-windows" && ENGINE_U="$RUNNER/lib/wine/x86_64-unix"

log "== removing game patches"
restore "$GAME/d3d12.dll"
restore "$GAME/d3d12core.dll"

log "== removing prefix patches"
restore "$SYS/d3d12_gptk.dll"
restore "$SYS/d3d12_gptk.so"
restore "$SYS/D3DMetal.framework"
restore "$SYS/dxgi.dll"
restore "$SYS/dxgi.so"
restore "$SYS/d3d12_wine.dll"
restore "$SYS/d3d12.dll"

if [ -n "$ENGINE_W" ]; then
  log "== removing engine patches"
  restore "$ENGINE_W/dxgi.dll"
  if [ -L "$ENGINE_U/dxgi.so" ]; then act rm -f "$ENGINE_U/dxgi.so"; log "  removed  $ENGINE_U/dxgi.so"; fi
  restore "$ENGINE_W/d3d12.dll"
  if [ -L "$ENGINE_U/d3d12.so" ]; then act rm -f "$ENGINE_U/d3d12.so"; log "  removed  $ENGINE_U/d3d12.so"; fi
fi

log "== restoring BridgePlay gamefix and Steam launch options"
restore "$BP_SUPPORT/gamefixes/$APPID.conf"
for f in "$STEAM_SUPPORT"/userdata/*/config/localconfig.vdf; do
  [ -f "$f.gptk-bak" ] && restore "$f"
done

log ""
[ "$MODE" = install ] && log "uninstalled. Steam 'Verify integrity of game files' restores any shipped file left over."
