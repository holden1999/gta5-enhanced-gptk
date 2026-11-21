# How the stack works

## The launch chain

```
Steam (Play) -> BridgePlay wrapper (compatibilitytools.d/bridgeplay-proton-26.3/run)
  -> CrossOver WineCX 26.3 loader
    -> PlayGTAV.exe
      -> Rockstar Games Launcher (CEF)  -> auth, title list
        -> GTA5_Enhanced.exe -nobattleye
          -> d3d12core.dll  (our proxy, SDK 619)
            -> system32\d3d12_gptk.dll   (Apple D3DMetal 4.0b3 PE)
              -> libd3dshared.dylib -> D3DMetal.framework -> Metal
          -> dxgi.dll (Apple) -> swapchain -> Metal
```

The game never imports `d3d12.dll` or `dxgi.dll` in its PE import table: it resolves
them at run time, and its Agility runtime loads `d3d12core.dll` from the game folder by
name.

## Wall 1: Wine's DXGI writes through a null pointer

Apple's `d3d12.dll` starts device creation with a warm-up:

```
0x180002a4a  lea rdx, [guid]      ; IID for the factory
0x180002a51  xor ecx, ecx         ; flags = 0
0x180002a53  xor r8d, r8d         ; ppFactory = NULL
0x180002a56  call CreateDXGIFactory2
```

Apple's `dxgi.dll` accepts a null out-parameter there. Wine's `dxgi.dll` stores the
factory through it and faults: `page fault on write ... at dxgi+0xf464`, every time,
before a window exists. That is why the runner's `dxgi` has to be Apple's, engine-wide:

```
lib/wine/x86_64-windows/dxgi.dll   <- apple_gptk/wine/x86_64-windows/dxgi.dll
lib/wine/x86_64-unix/dxgi.so      -> apple_gptk/.../dxgi.so -> libd3dshared.dylib
```

The unix half must be a symlink **into the GPTK tree**: `libd3dshared.dylib` opens
`D3DMetal.framework` through `@rpath` = `@loader_path`. A copy of the pair somewhere
else fails with `Assertion failed: (GFXTHandle && "Failed to dlopen D3DMetal")`.
`D3DMETAL_FRAMEWORK_PATH` overrides that search when a copy is unavoidable; the
installer instead copies the framework beside the unix half in the prefix, which is
where `@loader_path` looks for `system32\d3d12_gptk.so`.

## Wall 2: Wine aborts on a forwarder inside a native DLL

The game ships an Agility core whose only job is to forward:

```
d3d12core.dll
  [0] D3D12GetInterface -> forwarder "d3d12.D3D12GetInterface"
  [1] D3D12SDKVersion   -> 619
```

Wine resolves forwards for its own builtins, not for a native PE, so the call aborts:

```
wine: Call ... to unimplemented function d3d12core.dll.D3D12GetInterface, aborting
```

RAGE catches it as `ERR_GFX_INIT` ("reinstall latest DirectX runtime") and its render
thread then fails with second dialog (`ERR_GFX_RENDERTHREAD`). The fix is to
replace the forwarder with a real DLL. `src/d3d12_agility_proxy.c` is compiled twice:
`d3d12.dll` for the game root and `d3d12core.dll` with `D3D12SDKVersion` plus the six
core exports.

`d3d12core=n,b` in `DLLOVERRIDES` is required: without it Wine substitutes its own
`d3d12core.dll` stub and the abort returns.

## What the proxy implements

`D3D12GetInterface` answers three CLSIDs that D3DMetal 4.0b3 does not:

| CLSID | Returned object |
|---|---|
| `CLSID_D3D12SDKConfiguration` | `ID3D12SDKConfiguration` / `ID3D12SDKConfiguration1` |
| `CLSID_D3D12DeviceFactory` | `ID3D12DeviceFactory` |

`SetSDKVersion` returns `S_OK` and logs the request. `CreateDeviceFactory` and
`DeviceFactory::CreateDevice` forward to the real `D3D12CreateDevice` in
`system32\d3d12_gptk.dll`. Everything else (`D3D12GetDebugInterface`,
experimental features, root-signature serialization, core layered-device exports)
forwards too, or answers `E_NOTIMPL` when the real module has no such export.

The proxy routes by host executable: a name containing `gta5` or `playgtav` gets
Apple's `d3d12_gptk.dll`, everything else gets `d3d12_wine.dll` (the stock D3D12 kept
under a second name), so the launcher's own device queries keep the path they had
before the patch.

## Feature surface measured on this stack

| Item | Value |
|---|---|
| Device | 12_2 request accepted, 12_0/12_2 creation `S_OK` |
| Interfaces | `ID3D12Device` … `ID3D12Device10` |
| Shader model | 6.7 |
| Wave ops | yes, lane 32, 1024 total |
| Resource binding | Tier 3, tiled Tier 2 |
| Ray tracing | Tier 1.1, mesh shaders Tier 1 |
| Swapchain | flip-discard `S_OK`, present `S_OK`, device removed reason 0 |

`./verify.sh` re-measures the chain without launching the game.

## What is deliberately not done

- No DRM, Steam stub, Rockstar token or BattlEye file is modified. The game files are
  the shipped ones; only two DLLs are added beside the exe.
- No game archive is patched. The static-recompilation route is out of scope for a
  55 MB RAGE 9 binary with BattlEye and the Rockstar token chain.
- No Apple binary is redistributed. The installer reads D3DMetal from the BridgePlay
  runner the user installed.

## Reference build

macOS 27.2, Apple M4, BridgePlay with the `crossover-26.3.0.39832` runner, Apple GPTK
**4.0b3** (`libd3dshared.dylib` 250,672 B, `d3d12.dll` 196,608 B), game build 1.0.1158.16.
