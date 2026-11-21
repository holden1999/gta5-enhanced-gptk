# gta5-enhanced-gptk

Run **Grand Theft Auto V Enhanced** (Steam app 3240220, Windows build) on an Apple
Silicon Mac through **BridgePlay** and **Apple's Game Porting Toolkit (D3DMetal)**.

One command applies every patch this setup needs:

```sh
./install.sh
```

Then press Play in Steam. Story mode only: BattlEye stays disabled, GTA Online does
not work.

Nothing here is a crack. The game is bought on Steam, the launcher (Rockstar Games
Launcher) is the shipped one, and no DRM is touched. The repository ships no Apple
binaries and no game files: the installer copies D3DMetal out of **your own**
BridgePlay runner, and copies the patch DLLs built from `src/`.

## Requirements

| Item | Why |
|---|---|
| Apple Silicon Mac, macOS 15+ | D3DMetal needs Metal 3/4; tested on M4 with macOS 27.2 |
| Steam + **GTA V Enhanced** (3240220) installed | the game itself |
| [BridgePlay](https://bridgeplay.app) with the CrossOver **26.3**-style runner (`runners/*/CrossOver/lib64/apple_gptk`) | provides WineCX + Apple GPTK 4.0b3 |
| The game started **once** through BridgePlay | creates the prefix `compatdata/3240220/pfx` and the gamefix folder |
| `mingw-w64` (`brew install mingw-w64`) | only to rebuild `dist/`; not needed to install |

The prefix must already exist. If `install.sh` says `no Wine prefix`, launch the game
once (it will fail with a DirectX dialog), close it, and run the installer again.

## Install

```sh
git clone https://github.com/holden1999/gta5-enhanced-gptk.git
cd gta5-enhanced-gptk
./install.sh            # or: ./install.sh --check   (report only)
./verify.sh             # optional: proves the D3D12 chain without launching the game
```

`--dry-run` prints every write and changes nothing. `--check` reports what is already
in place. Every replaced file is copied once to `<file>.gptk-bak`; `./uninstall.sh`
restores them.

## What the installer changes

**BridgePlay runner** (`~/Library/Application Support/bridgeplay/runners/<runner>/CrossOver`)

| File | Change |
|---|---|
| `lib/wine/x86_64-windows/dxgi.dll` | replaced with Apple's D3DMetal `dxgi` |
| `lib/wine/x86_64-unix/dxgi.so` | symlink to `lib64/apple_gptk/.../dxgi.so` → `libd3dshared.dylib` |
| `lib/wine/x86_64-windows/d3d12.dll` | symlink to the Apple D3DMetal PE (already so on 26.3) |

**Prefix** (`compatdata/3240220/pfx/drive_c/windows/system32`)

| File | Change |
|---|---|
| `d3d12_gptk.dll`, `d3d12_gptk.so` | Apple D3DMetal pair + unix half |
| `D3DMetal.framework` | copied next to the unix half so `@loader_path` resolves it |
| `dxgi.dll`, `dxgi.so` | Apple D3DMetal DXGI pair |
| `d3d12_wine.dll` | stock D3D12 kept under a second name for non-game hosts |
| `d3d12.dll` | agility proxy (see below) |

**Game folder** (`steamapps/common/Grand Theft Auto V Enhanced`)

| File | Change |
|---|---|
| `d3d12.dll` | agility proxy built from `src/d3d12_agility_proxy.c` |
| `d3d12core.dll` | the same proxy exporting `D3D12SDKVersion` = 619 and the core exports |

**BridgePlay gamefix** (`~/Library/Application Support/bridgeplay/gamefixes/3240220.conf`)

```ini
NVAPI=0
BACKEND=d3dmetal
ROCKSTAR_SHIM=1
ARGS=-nobattleye
DLLOVERRIDES=d3d12=n,b;d3d12core=n,b;d3d11=n,b;d3d10core=n,b
```

**Steam launch options** (`localconfig.vdf`, only when Steam is closed)

```
CX_GRAPHICS_BACKEND=d3dmetal %command%
```

## Why this is needed

GTA V Enhanced is Direct3D 12 only and activates the **D3D12 Agility SDK** at startup.
Three things break on a stock BridgePlay bottle:

1. **Wine's `dxgi` crashes Apple's D3D12.** D3DMetal's `D3D12CreateDevice` calls
   `CreateDXGIFactory2(0, &iid, NULL)`; Wine's DXGI writes through that null. Apple's
   DXGI treats it as a warm-up. The runner must use Apple's `dxgi`, not Wine's.
2. **Wine cannot resolve a PE forwarder from a native DLL.** The shipped
   `d3d12core.dll` only forwards `D3D12GetInterface` to `d3d12.dll`. Wine aborts with
   `unimplemented function d3d12core.dll.D3D12GetInterface`, and the game shows
   `ERR_GFX_INIT — reinstall latest DirectX runtime`. The proxy implements the export.
3. **The Agility stub must answer.** `ID3D12SDKConfiguration`, `ID3D12SDKConfiguration1`
   and `ID3D12DeviceFactory` do not exist in D3DMetal 4.0b3. The proxy supplies them
   and forwards device creation to the real D3DMetal PE. `D3D12SDKVersion` = 619 is
   the value the shipped core reported.

```
Game -> d3d12core.dll (proxy, SDK 619)
      -> D3D12GetInterface / SetSDKVersion / CreateDeviceFactory
      -> system32\d3d12_gptk.dll (Apple D3DMetal 4.0b3)
      -> Metal
Game -> dxgi (Apple) -> swapchain -> Metal
```

Full detail: [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).
Symptom to fix: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Verify

`./verify.sh` runs a probe that walks the same path the game walks: core export,
`D3D12GetInterface`, `SetSDKVersion`, device factory, device, queue, Apple DXGI factory,
flip-discard swapchain, present, device-removed reason. It prints `PASS` or the first
failing step. It does not launch the game.

Expected on a working install:

```
d3d12core!D3D12SDKVersion                  ok
  SDK version = 619
D3D12GetInterface(SDKConfiguration)        ok   0x00000000
DeviceFactory::CreateDevice(12_0)          ok   0x00000000
CreateSwapChainForHwnd(flip-discard)       ok   0x00000000
Present                                    ok   0x00000000
PASS
```

## Rebuilding the patch DLLs

```sh
brew install mingw-w64
./build.sh
```

`build.sh` writes `dist/d3d12core.dll` and a copy as `dist/d3d12.dll`. The same file
serves both names; `d3d12core.dll` is the one that must export `D3D12SDKVersion`,
otherwise the game reads a garbage version.

## After a BridgePlay update or a Steam file check

- A **BridgePlay runner update** replaces the runner's `dxgi.dll`: run `./install.sh` again.
- **Steam → Verify integrity of game files** restores the shipped `d3d12core.dll`
  forwarder: run `./install.sh` again.
- `./install.sh` is safe to run at any time; it skips files whose content already matches.

## Uninstall

```sh
./uninstall.sh          # --check to preview
```

Restores every `.gptk-bak`, removes the staged DLLs and the gamefix, and puts the
Steam launch options back.

## Notes and limits

- **Story mode only.** `-nobattleye` is required; BattlEye needs a Windows kernel driver.
- **Not a native port.** D3D12 is translated by D3DMetal; shader translation warnings
  and pipeline-compile hitches are normal for this path.
- **Do not mix DXVK and D3DMetal for this title.** The launcher's CEF can fall back to
  software rendering; the game itself must get Apple's `d3d12` and `dxgi`.
- **D3DMetal version matters.** The installer reads the version from the runner and
  copies that same framework into the prefix; a mixed 3.0/4.0 pair fails with
  `Assertion failed: (GFXTHandle && "Failed to dlopen D3DMetal")`.

## License

MIT — see [LICENSE](LICENSE). D3DMetal, GPTK and BridgePlay belong to their owners and
are not distributed here.
