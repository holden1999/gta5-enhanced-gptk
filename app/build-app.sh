#!/usr/bin/env bash
# Build ~/Applications/GTA V Enhanced.app (a launcher shell, not a native port).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$HOME/Applications/GTA V Enhanced.app"
ICON="$HOME/Library/Application Support/bridgeplay/launchers/3240220/game.icns"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$HERE/Info.plist" "$APP/Contents/Info.plist"
cp "$HERE/launch" "$APP/Contents/MacOS/launch"
chmod +x "$APP/Contents/MacOS/launch"
[ -f "$ICON" ] && cp "$ICON" "$APP/Contents/Resources/game.icns"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
echo "built $APP"
"$APP/Contents/MacOS/launch" --check
