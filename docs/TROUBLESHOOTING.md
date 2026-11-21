# Troubleshooting

Run `./install.sh --check` first: it names the missing piece. Then `./verify.sh` to see
which D3D12 step fails, before touching anything.

## Where the logs are

| File | What it answers |
|---|---|
| `steamapps/compatdata/3240220/bridgeplay-run.log` | launch arguments, environment, applied gamefix keys, runner build |
| `~/Library/Application Support/bridgeplay/launchers/3240220/bridgeplay-wine.log` | Wine stderr, Metal/overlay messages |
| `pfx/drive_c/users/steamuser/Documents/Rockstar Games/Launcher/launcher.log` | `[gamelaunch] Found game window:` and the exit code |
| `pfx/drive_c/agility-proxy.log` | every proxy export call, the adapter pointer and the `HRESULT` |
| `pfx/.../AppData/Local/Rockstar Games/GTAV Enhanced/CrashLogs/` | minidump and `crashcontext.log` (adapter, factory state) |

## Symptoms

### `ERR_GFX_INIT` — "Failed to initialize graphics device. Please reboot or reinstall latest DirectX runtime"

The game's Agility core could not answer `D3D12GetInterface`. Either the shipped
forwarder is back or the override is missing:

```sh
grep DLLOVERRIDES "$HOME/Library/Application Support/bridgeplay/gamefixes/3240220.conf"
# must contain d3d12core=n,b
shasum -a 256 "/path/to/Grand Theft Auto V Enhanced/d3d12core.dll"   # must match dist/
```

A Steam "Verify integrity" restores the shipped forwarder. Run `./install.sh` again.

### `ERR_GFX_D3D_NOD3D12` and a window with that title

The game sees an adapter it cannot use. Two causes:

- `NVAPI=1` advertises an NVIDIA adapter that D3DMetal does not back. The installer
  writes `NVAPI=0`.
- `dxgi.dll` in the runner is still Wine's. Check the third line of
  `bridgeplay-wine.log`: `graphics: backend=d3dmetal` alone does not prove Apple's
  `dxgi` is loaded.

### `ERR_SYS_SYSREQ_GPU`

The title fell back to Wine's vkd3d or DXVK path: the prefix got Wine's `d3d12` instead
of Apple's. Re-run `./install.sh`; confirm `system32\d3d12_gptk.dll` and
`system32\d3d12_gptk.so` exist and the framework sits beside them.

### `Assertion failed: (GFXTHandle && "Failed to dlopen D3DMetal")`

`libd3dshared.dylib` cannot open the framework. The unix half is not next to a
`D3DMetal.framework`, or the framework version differs from the one the runner was
built against. `./install.sh` copies the runner's own framework into the prefix; do not
mix a 3.0 framework with 4.0 shims.

### The game exits about 3 seconds after Play, no window

The proxy loaded but the real D3DMetal DLL is missing or is another proxy. Check
`agility-proxy.log`:

```
proxy: host=gta5_enhanced.exe provider=d3d12_gptk.dll
proxy: gptk load failed, err=...
```

Re-run `./install.sh`. If `system32\d3d12_gptk.dll` is a small file (~90 KB) it is the
stock D3D12, not D3DMetal (196,608 B).

### The launcher window is blank, or authentication never finishes

The CEF launcher renders through D3D11. With Apple's `dxgi` in the runner it may fall
back to software drawing. The wrapper still starts the game once the token is valid;
if sign-in is stuck, check `launcher.log` for `[subsystem] Initializing group 1...` and
confirm `ROCKSTAR_SHIM=1` is in the gamefix.

### The game runs but the intro stutters and pipelines "compile" for seconds

That is shader translation in D3DMetal, not a broken install. Watch the Metal HUD
(`MTL_HUD_ENABLED=1` in the global gamefix) to separate GPU time from a compile stall.

## Reading the proxy log correctly

`agility-proxy.log` grows one block per process:

```
proxy: host=<exe> provider=<d3d12_gptk.dll | d3d12_wine.dll>
proxy: real d3d12 at <path>
proxy: D3D12CreateDevice(adapter=..., level=0xc000) -> 0x00000000
```

`0x00000000` is success. A `gta5_enhanced.exe` block that stops right after
`provider=` means the real DLL failed to load. A block that shows
`D3D12GetInterface(CLSID_D3D12SDKConfiguration)` proves the Agility path reached the
proxy.

## Starting over

```sh
./uninstall.sh
# Steam: right-click the game -> Properties -> Installed Files -> Verify integrity
./install.sh
```

BridgePlay itself is untouched by the uninstaller except for the per-app gamefix.
