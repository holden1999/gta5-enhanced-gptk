#!/usr/bin/env bash
# Rebuild dist/d3d12.dll and dist/d3d12core.dll from src/.
# Requires mingw-w64: brew install mingw-w64
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-x86_64-w64-mingw32-gcc}"

command -v "$CC" >/dev/null || { echo "missing $CC (brew install mingw-w64)" >&2; exit 1; }

mkdir -p "$HERE/dist"
"$CC" -O2 -shared -o "$HERE/dist/d3d12core.dll" "$HERE/src/d3d12_agility_proxy.c" \
  -luuid -Wl,--add-stdcall-alias -Wl,--enable-stdcall-fixup
cp -f "$HERE/dist/d3d12core.dll" "$HERE/dist/d3d12.dll"

echo "built:"
shasum -a 256 "$HERE"/dist/*.dll
x86_64-w64-mingw32-objdump -p "$HERE/dist/d3d12core.dll" | grep -E 'D3D12SDKVersion|D3D12GetInterface' | head -3
