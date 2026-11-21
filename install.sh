#!/usr/bin/env bash
# gta5-enhanced-gptk — one-command setup for GTA V Enhanced (Steam 3240220) on
# Apple Silicon, running under BridgePlay's WineCX with Apple's D3DMetal (GPTK).
#
# Usage:  ./install.sh [--check] [--dry-run]
#         ./install.sh --check     report what is present, change nothing
#         ./install.sh --dry-run   print every write, change nothing
#
# The script is idempotent: every file it replaces is copied once to <file>.gptk-bak
# and skipped afterwards when the content already matches.
set -euo pipefail

APPID=3240220
GAMENAME="Grand Theft Auto V Enhanced"
STEAM_SUPPORT="${STEAM_SUPPORT:-$HOME/Library/Application Support/Steam}"
BP_SUPPORT="${BP_SUPPORT:-$HOME/Library/Application Support/bridgeplay}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$HERE/dist"
MODE=install

for arg in "$@"; do
  case "$arg" in
    --check) MODE=check ;;
    --dry-run) [ "$MODE" = install ] && MODE=dry ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) printf 'unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

log()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

act() {
  case "$MODE" in
    check) printf '  would  %s\n' "$*" ;;
    dry)   printf '  dry    %s\n' "$*" ;;
    *)     "$@" ;;
  esac
}

backup() {
  local f="$1"
  [ -e "$f" ] || return 0
  [ -e "$f.gptk-bak" ] && return 0
  act cp -p "$f" "$f.gptk-bak"
}

stage_file() {
  local src="$1" dst="$2" label="$3"
  if [ -f "$dst" ] && cmp -s "$src" "$dst"; then
    log "  ok      $label"
    return 0
  fi
  [ -f "$src" ] || die "missing source file: $src"
  backup "$dst"
  act mkdir -p "$(dirname "$dst")"
  act cp -f "$src" "$dst"
  if [ "$MODE" = install ]; then log "  staged  $label"; else log "  plan    $label"; fi
}

link_so() {
  local target="$1" link="$2" label="$3"
  if [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]; then
    log "  ok      $label"
    return 0
  fi
  if [ -e "$link" ] && [ ! -L "$link" ]; then
    backup "$link"
    act rm -f "$link"
  elif [ -L "$link" ]; then
    act rm -f "$link"
  fi
  act ln -s "$target" "$link"
  if [ "$MODE" = install ]; then log "  linked  $label"; else log "  plan    $label"; fi
}

# --- 1. locate the install ---------------------------------------------------
log "== GTA V Enhanced / BridgePlay / D3DMetal setup"

[ -d "$STEAM_SUPPORT" ] || die "Steam not found at $STEAM_SUPPORT"

LIB=""
VDF="$STEAM_SUPPORT/steamapps/libraryfolders.vdf"
CANDIDATES=("$STEAM_SUPPORT")
if [ -f "$VDF" ]; then
  while IFS= read -r p; do CANDIDATES+=("$p"); done < <(sed -n 's/.*"path"[[:space:]]*"\(.*\)".*/\1/p' "$VDF")
fi
for c in "${CANDIDATES[@]}"; do
  if [ -d "$c/steamapps/common/$GAMENAME" ]; then LIB="$c"; break; fi
done
[ -n "$LIB" ] || die "$GAMENAME is not installed in any Steam library (checked ${#CANDIDATES[@]} roots)"

GAME="$LIB/steamapps/common/$GAMENAME"
PFX="$LIB/steamapps/compatdata/$APPID/pfx"
SYS="$PFX/drive_c/windows/system32"

[ -d "$PFX" ] || die "no Wine prefix at $PFX — run the game once from Steam through BridgePlay first"

RUNNER=""
for c in "$BP_SUPPORT"/runners/*/CrossOver; do
  [ -d "$c/lib64/apple_gptk/wine/x86_64-windows" ] || continue
  [ -f "$c/lib64/apple_gptk/external/libd3dshared.dylib" ] || continue
  RUNNER="$c"
done
[ -n "$RUNNER" ] || die "no BridgePlay runner with Apple GPTK found under $BP_SUPPORT/runners/*/CrossOver"

GPTK="$RUNNER/lib64/apple_gptk"
FW="$GPTK/external/D3DMetal.framework"
FWVER="$(plutil -extract CFBundleShortVersionString raw "$FW/Versions/A/Resources/Info.plist" 2>/dev/null || echo unknown)"
ENGINE_W="$RUNNER/lib/wine/x86_64-windows"
ENGINE_U="$RUNNER/lib/wine/x86_64-unix"

log "  game    $GAME"
log "  prefix  $PFX"
log "  runner  $(basename "$(dirname "$RUNNER")")  (D3DMetal $FWVER)"

# --- 2. engine-level D3D: Apple dxgi + Apple d3d12 ---------------------------
log "== engine: point dxgi and d3d12 at Apple D3DMetal"
stage_file "$GPTK/wine/x86_64-windows/dxgi.dll" "$ENGINE_W/dxgi.dll" "engine dxgi.dll (Apple D3DMetal)"
link_so   "$GPTK/wine/x86_64-unix/dxgi.so"      "$ENGINE_U/dxgi.so" "engine dxgi.so -> libd3dshared"

if [ -L "$ENGINE_W/d3d12.dll" ] && [ "$(readlink "$ENGINE_W/d3d12.dll")" = "$GPTK/wine/x86_64-windows/d3d12.dll" ]; then
  log "  ok      engine d3d12.dll -> Apple D3DMetal"
else
  if [ -f "$ENGINE_W/d3d12.dll" ] && [ ! -L "$ENGINE_W/d3d12.dll" ]; then
    backup "$ENGINE_W/d3d12.dll"
    act rm -f "$ENGINE_W/d3d12.dll"
  fi
  act ln -s "$GPTK/wine/x86_64-windows/d3d12.dll" "$ENGINE_W/d3d12.dll"
  log "  linked  engine d3d12.dll -> Apple D3DMetal"
fi
if [ ! -L "$ENGINE_U/d3d12.so" ]; then
  [ -f "$ENGINE_U/d3d12.so" ] && backup "$ENGINE_U/d3d12.so" && act rm -f "$ENGINE_U/d3d12.so"
  act ln -s "$GPTK/wine/x86_64-unix/d3d12.so" "$ENGINE_U/d3d12.so"
  log "  linked  engine d3d12.so -> libd3dshared"
else
  log "  ok      engine d3d12.so -> libd3dshared"
fi

# --- 3. prefix-level D3D ----------------------------------------------------
log "== prefix: D3DMetal pair, framework, dxgi pair, stock fallback"
stage_file "$GPTK/wine/x86_64-windows/d3d12.dll" "$SYS/d3d12_gptk.dll" "system32 d3d12_gptk.dll (Apple D3DMetal)"
stage_file "$GPTK/external/libd3dshared.dylib"  "$SYS/d3d12_gptk.so"  "system32 d3d12_gptk.so"

if [ -d "$SYS/D3DMetal.framework" ] && \
   [ "$(plutil -extract CFBundleShortVersionString raw "$SYS/D3DMetal.framework/Versions/A/Resources/Info.plist" 2>/dev/null || echo none)" = "$FWVER" ]; then
  log "  ok      system32 D3DMetal.framework ($FWVER)"
else
  if [ -d "$SYS/D3DMetal.framework" ] && [ ! -d "$SYS/D3DMetal.framework.gptk-bak" ]; then
    act mv "$SYS/D3DMetal.framework" "$SYS/D3DMetal.framework.gptk-bak"
  fi
  [ -d "$SYS/D3DMetal.framework" ] && act rm -rf "$SYS/D3DMetal.framework"
  act cp -R "$FW" "$SYS/D3DMetal.framework"
  if [ "$MODE" = install ]; then log "  staged  system32 D3DMetal.framework ($FWVER)"; else log "  plan    system32 D3DMetal.framework ($FWVER)"; fi
fi

# the launcher path: keep Wine's own d3d12 under a second name, then install the proxy
if [ ! -f "$SYS/d3d12_wine.dll" ] && [ -f "$SYS/d3d12.dll" ]; then
  act cp -p "$SYS/d3d12.dll" "$SYS/d3d12_wine.dll"
  if [ "$MODE" = install ]; then log "  staged  system32 d3d12_wine.dll (stock fallback)"; else log "  plan    system32 d3d12_wine.dll (stock fallback)"; fi
fi
stage_file "$DIST/d3d12.dll" "$SYS/d3d12.dll" "system32 d3d12.dll (agility proxy)"

stage_file "$GPTK/wine/x86_64-windows/dxgi.dll" "$SYS/dxgi.dll" "system32 dxgi.dll (Apple D3DMetal)"
stage_file "$GPTK/external/libd3dshared.dylib"  "$SYS/dxgi.so"  "system32 dxgi.so"

# --- 4. game folder ---------------------------------------------------------
log "== game folder: agility proxy as d3d12.dll and d3d12core.dll"
stage_file "$DIST/d3d12.dll"     "$GAME/d3d12.dll"     "game d3d12.dll"
stage_file "$DIST/d3d12core.dll" "$GAME/d3d12core.dll" "game d3d12core.dll (exports D3D12SDKVersion 619)"

# --- 5. BridgePlay gamefix --------------------------------------------------
log "== BridgePlay gamefix"
CONF="$BP_SUPPORT/gamefixes/$APPID.conf"
CONF_BODY="# GTA V Enhanced under BridgePlay + Apple D3DMetal (managed by gta5-enhanced-gptk)
NVAPI=0
BACKEND=d3dmetal
ROCKSTAR_SHIM=1
ARGS=-nobattleye
# d3d12core=n,b sends the game's Agility activation to the proxy; without it Wine loads its
# own stub, whose D3D12GetInterface aborts with \"unimplemented function\" (ERR_GFX_INIT).
DLLOVERRIDES=d3d12=n,b;d3d12core=n,b;d3d11=n,b;d3d10core=n,b
"
if [ -f "$CONF" ] && [ "$(cat "$CONF")" = "$CONF_BODY" ]; then
  log "  ok      gamefixes/$APPID.conf"
else
  backup "$CONF"
  if [ "$MODE" = check ] || [ "$MODE" = dry ]; then
    act write "$CONF"
  else
    mkdir -p "$(dirname "$CONF")"
    printf '%s' "$CONF_BODY" > "$CONF"
  fi
  if [ "$MODE" = install ]; then log "  wrote   gamefixes/$APPID.conf"; else log "  plan    gamefixes/$APPID.conf"; fi
fi

# --- 6. Steam launch options ------------------------------------------------
log "== Steam launch options"
if [ -x "$HERE/tools/set-launch-options.py" ]; then
  if [ "$MODE" != install ]; then
    "$HERE/tools/set-launch-options.py" --appid "$APPID" --check || true
  else
    "$HERE/tools/set-launch-options.py" --appid "$APPID" --value 'CX_GRAPHICS_BACKEND=d3dmetal %command%' || \
      warn "launch options unchanged; set them by hand: CX_GRAPHICS_BACKEND=d3dmetal %command%"
  fi
fi

# --- 7. summary -------------------------------------------------------------
log ""
if [ "$MODE" = check ]; then
  log "check complete: no files were changed."
elif [ "$MODE" = dry ]; then
  log "dry run complete: no files were changed."
else
  log "done. Start Grand Theft Auto V Enhanced from Steam."
  log "story mode only: BattlEye is disabled by -nobattleye (GTA Online will not work)."
fi
