# From `.exe` to `.app`: what a native build would take

Short version: **this title cannot be recompiled into a native arm64 `.app` today.** What can be
built, and is built, is a native `.app` shell that starts the patched WineCX/D3DMetal chain.

## Why a native rebuild is not a build step

| Fact | Value |
|---|---|
| Executable | `GTA5_Enhanced.exe`, 55,819,384 bytes, PE32+ x86-64 |
| Engine | RAGE 9, Direct3D 12 only, Agility SDK activation at startup |
| Anti-cheat | BattlEye (kernel service on Windows) |
| DRM chain | Steam stub `steam_api64.dll`, Rockstar Games Launcher token |
| Running cost today | ~216% CPU, ~848 MB RSS, all of it x86-64 under Rosetta |

Three independent blockers:

1. **Scale.** The local x86-to-arm64 lifter used for GTA IV (`x86recomp`) handles a 9 MB
   1.0.8.0 binary: ~52k lifted functions, 452 left unimplemented, and a SIGBUS stop in the
   mount table. This binary is six times larger, with a D3D12 renderer instead of D3D9, so
   the lift alone is a multi-month project with an unknown completion rate.
2. **Renderer.** A lifted core still calls `ID3D12Device` vtables. A native build needs a
   Metal renderer written against the game's render-device abstraction. D3DMetal does that
   translation today; re-doing it in-tree is a renderer port, not a recompile.
3. **DRM.** A native binary would have to satisfy the Steam stub and the RGL token without
   the Windows modules. Removing that protection is out of scope by policy, so the only
   lawful native build is one that keeps running the Windows DRM modules, which puts the
   process back under Wine.

## What actually improves Metal performance on this stack

Measured levers, in order of effect per unit of risk:

1. **DLSS through D3DMetal's `nvapi64` shim** (`D3DM_ENABLE_METALFX=1`, in-game DLSS
   Quality/Balanced). D3DMetal maps it to MetalFX spatial upscaling; at 1440p output it is
   the largest single win. It needs `NVAPI=1` in the gamefix, which was disabled during
   bring-up because it advertised an adapter the old DXGI path could not use. That cause is
   gone since Apple's `dxgi` replaced Wine's, so this is now a candidate for an A/B run.
2. **Internal resolution scale** in `settings.xml` (`ResScalingType`) with the panel at
   2560x1440. Rendering at 1080p and letting MetalFX scale is cheaper than 1440p native.
3. **Metal HUD measurement**, so a change is judged on GPU time and frame interval instead
   of feel: `./perf.sh hud on`, then read the overlay on the next launch.
4. **Frame pacing**: cap below the panel refresh to keep GPU time under the frame interval;
   uncapped flip-discard swapchains show more stutter under translation.

## What the `.app` in `~/Applications` is

`GTA V Enhanced.app` runs a preflight (game folder patched, `d3d12core=n,b` present) and then
opens `steam://rungameid/3240220`. Steam, BridgePlay and the gamefix do the rest, so the Steam
stub and the RGL token see a normal launch. It is a launcher, not a binary translation: it
removes no Rosetta overhead and is not a native port.

## If a native port is still wanted

The honest scope is a new project, not a patch to this one:

```
1. Lift GTA5_Enhanced.exe to arm64 (x86recomp lineage), stop at the first real stop,
   publish the coverage number. Months, and the number may never reach 100%.
2. Replace the D3D12 render device with a Metal implementation, feature by feature
   (PSO cache, descriptors, barriers, swapchain, shader translation).
3. Keep the Windows DRM modules running, or drop the title from Steam.
```

None of those steps can be validated in a single session, and step 3 conflicts with the
no-DRM-work rule. That is why the shipped path stays on D3DMetal.
