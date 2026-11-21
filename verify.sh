#!/usr/bin/env bash
# Run the D3D12 chain probe against the installed prefix. Needs no game launch.
#   ./verify.sh
set -euo pipefail

APPID=3240220
GAMENAME="Grand Theft Auto V Enhanced"
STEAM_SUPPORT="${STEAM_SUPPORT:-$HOME/Library/Application Support/Steam}"
BP_SUPPORT="${BP_SUPPORT:-$HOME/Library/Application Support/bridgeplay}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CANDIDATES=("$STEAM_SUPPORT")
[ -f "$STEAM_SUPPORT/steamapps/libraryfolders.vdf" ] && while IFS= read -r p; do CANDIDATES+=("$p"); done \
  < <(sed -n 's/.*"path"[[:space:]]*"\(.*\)".*/\1/p' "$STEAM_SUPPORT/steamapps/libraryfolders.vdf")
LIB=""; for c in "${CANDIDATES[@]}"; do [ -d "$c/steamapps/common/$GAMENAME" ] && LIB="$c" && break; done
[ -n "$LIB" ] || { echo "game not installed" >&2; exit 1; }

PFX="$LIB/steamapps/compatdata/$APPID/pfx"
RUNNER=""; for c in "$BP_SUPPORT"/runners/*/CrossOver; do [ -d "$c/lib64/apple_gptk" ] && RUNNER="$c"; done
[ -n "$RUNNER" ] || { echo "no BridgePlay runner with Apple GPTK" >&2; exit 1; }

TMP="$PFX/drive_c/gta5-verify"
rm -rf "$TMP"; mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
cp -f "$HERE/dist/verify.exe" "$TMP/gta5-verify.exe"
cp -f "$HERE/dist/d3d12core.dll" "$TMP/d3d12core.dll"

echo "prefix: $PFX"
echo "runner: $(basename "$(dirname "$RUNNER")")"
echo
cd "$TMP"
set +e
WINEPREFIX="$PFX" WINEMSYNC=1 CX_GRAPHICS_BACKEND=d3dmetal WINEDEBUG=-all \
WINEDLLOVERRIDES="d3d12=n,b;d3d12core=n,b;d3d11=n,b;d3d10core=n,b" \
"$RUNNER/lib/wine/x86_64-unix/wine" 'C:\gta5-verify\gta5-verify.exe'
rc=$?
set -e
cat "$TMP/verify.log" 2>/dev/null
exit $rc
