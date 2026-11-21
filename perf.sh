#!/usr/bin/env bash
# Measure the running game, and toggle the Metal HUD in the per-app gamefix.
#   ./perf.sh            current process stats and HUD state
#   ./perf.sh hud on     enable MTL_HUD_ENABLED=1 for the next launch
#   ./perf.sh hud off    disable it
set -euo pipefail

APPID=3240220
BP_SUPPORT="${BP_SUPPORT:-$HOME/Library/Application Support/bridgeplay}"
CONF="$BP_SUPPORT/gamefixes/$APPID.conf"

hud_state() { grep -q '^MTL_HUD_ENABLED=1' "$CONF" 2>/dev/null && echo on || echo off; }

set_hud() {
  local want="$1"
  [ -f "$CONF" ] || { echo "missing $CONF (run install.sh once)" >&2; exit 1; }
  [ -f "$CONF.gptk-bak" ] || cp -p "$CONF" "$CONF.gptk-bak"
  if grep -q '^MTL_HUD_ENABLED=' "$CONF"; then
    sed -i '' "s/^MTL_HUD_ENABLED=.*/MTL_HUD_ENABLED=$want/" "$CONF"
  else
    printf 'MTL_HUD_ENABLED=%s\n' "$want" >> "$CONF"
  fi
  echo "MTL_HUD_ENABLED=$want (takes effect on the next launch)"
  [ "$want" = 1 ] && echo "read GPU time vs frame interval in the overlay; press Esc, then Shift+F1 to cycle tabs" || true
}

case "${1:-stats}" in
  hud) set_hud "${2:-on}" ;;
  stats)
    PID="$(pgrep -f 'GTA5_Enhanced.exe' | head -1 || true)"
    echo "HUD: $(hud_state)"
    if [ -z "$PID" ]; then echo "game not running"; exit 0; fi
    ps -o pid,%cpu,rss,etime -p "$PID"
    echo "threads: $(ps -M -p "$PID" | tail -n +2 | wc -l | tr -d ' ')"
    echo
    echo "RSS is in KB. The WineCX loader and the game are x86-64, so everything"
    echo "below D3DMetal runs under Rosetta; MTL_HUD_ENABLED=1 separates GPU time"
    echo "from translation time on the next run."
    ;;
  *) echo "usage: $0 [stats|hud on|hud off]" >&2; exit 2 ;;
esac
