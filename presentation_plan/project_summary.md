# 3DGS Vulkan Visualizer — Project Summary (presentation source)

A from-scratch **3D Gaussian Splatting** renderer in **C++20 + Vulkan 1.3**,
starting from the INRIA 3DGS algorithm/reference and evolving into a desktop
viewer, a tiled compute rasterizer, and a foveated OpenXR PCVR visualizer for
the Quest 3.

- Hardware: NVIDIA RTX 5070 Laptop GPU (driver 1.4.329), Linux, Quest 3 through
  WiVRn (OpenXR PCVR streaming).
- Desktop window: 1600x900.
- VR runtime: WiVRn (Monado-based) OpenXR runtime; USB via `adb reverse
  tcp:9757 tcp:9757` (or Wi-Fi).
- Code: `src/`, shaders in `shaders/`, launch scripts in `scripts/`.
- Git history: 35 commits, 3DGS pipeline -> optimizations -> tiled -> VR ->
  foveation -> resolution/screenshots.

All performance numbers below were measured on the RTX 5070 Laptop with the
current code, unless marked as a historical delta from a commit message.

---

## 1. The starting point: what "base 3DGS" is

3DGS represents a scene as millions of explicit **anisotropic 3D Gaussians**.
Each Gaussian stores:

- 3D position (3 floats)
- 3D scale (3)
- rotation quaternion (4)
- opacity (1)
- spherical-harmonics colour (degree 3: 48 coefficients)

The reference pipeline (Kerbl et al., SIGGRAPH 2023, CUDA) does:

1. **Project** each 3D Gaussian: 3D covariance -> 2D covariance via the EWA
   Jacobian; the 2D conic defines a screen-space ellipse.
2. **Depth sort** all Gaussians back-to-front (alpha blending requires order).
3. **Rasterize** the splats with alpha blending.
4. **Evaluate SH** per Gaussian/view direction for view-dependent colour.

The academic implementation is CUDA-only. The project goal was to reimplement
the same visual result cross-vendor in Vulkan, without an engine, so the
pipeline stages are explicit and controllable.

Key design decision from day one: **keep rendering separate from
presentation**. The renderer draws into targets supplied by an
`IPresentation`; this is what later made the VR path possible without
rewriting the renderer.

---

## 2. Timeline / roadtrip

| Phase | Commit(s) | What was added | Outcome |
|---|---|---|---|
| 0. Scaffold | `bb5f4ab` | CMake, Vulkan instance/device/swapchain, VMA, pass framework, ImGui, background | A running Vulkan app |
| 1. Splat viewer | `205fa36`, `d9b7bfc`, `6befb8e` | PLY loader, cameras.json, pre-activation, EWA instanced quads, CPU sort, orbit/fly camera | First real 3DGS desktop viewer |
| 2a. SH colour | `285e4da`, `4b2b904` | Degree-3 SH in the vertex shader, GUI degree slider | View-dependent colour |
| 2b. GPU sort | `5b30fed` | GPU LSD radix sort (keygen + 4x8-bit stable histogram/scan/scatter) | room ~11 FPS -> ~51 FPS |
| Desktop optimizations | `1a8aedd`, `c32b741` | Tight per-axis quads; frustum culling + indirect draw | Raster roughly halved; off-screen geometry culled |
| Stability / sort | `77f3db7`, `33fba23`, `3466b26` | Deterministic stable radix, warp-match ranking, idle-skip | Flicker fixed; sort 2.5 -> 0.87 ms (room) |
| Tiled rasterizer | `bca6b14`, `2ed77e7` | Compute project -> bin -> sort -> 16x16 tile raster with early-out -> composite | Raster 6.3 -> 4.6 ms (room), but total comparable/slower |
| M1. Multi-view | `d966c4e` | `PresentFrame` with N views, per-view cameras/targets, sort once | Desktop stereo test; renderer ready for VR |
| M2. OpenXR stereo | `38b5f8c`, `561e49d` | XrContext/XrSwapchain/XrPresentation, per-eye swapchains, stereo projection layer | VR on Quest 3 through WiVRn |
| VR correctness/UX | `5431251`, `3b34b0f`, `c044a89`, `cdc7a81`, `f510332` | Scene names, floater culling, average-up leveling, world scale, A/B controller actions | Usable PCVR experience |
| VRS | `9a35bc5`, `940d4ba` | Uniform and fixed-foveated fragment shading rate | 19.1 -> 12.3 ms at 0.5 scale |
| Blur / refresh / screenshots / resolution | `e057123`, `297b2b8`, `56f5d6e`, `0fb5db6`, `691d46a` | 3x3 -> 5x5 foveated blur, 72 Hz request, dual-eye PNG, resolution scale | Final quality/perf knobs |

Current default: `--vr-foveation 4` uses a **0.75 render scale**
(1856x1944/eye), 5x5 blur, 72 Hz request.

---

## 3. Implementation details

### 3.1 Loading and data path

- `PlyLoader`: header-driven INRIA `.ply` parser; detects SH degree; supports
  the official pretrained scenes.
- `CameraLoader`: reads `cameras.json` for the initial pose.
- CPU pre-activation: sigmoid for opacity, exp for scale, quaternion
  normalization.
- CPU precomputed 3D covariance.
- Packed GPU layout: 52 B/Gaussian (position, scale, rotation, opacity, SH).
- 13 scenes available (`room`, `counter`, `bonsai`, `garden`, `playroom`,
  `drjohnson`, ...), up to ~5.8 M Gaussians (garden).

Scene sizes used in the presentation:

| Scene | Gaussians | Notes |
|---|---:|---|
| room | 1,593,376 | main perf scene |
| counter | 1,222,956 | near the 72 Hz budget |
| bonsai | 1,244,819 | small/fast |
| playroom | 2,546,116 | off-screen geometry for culling demo |
| drjohnson | 3,405,153 | culling demo |
| garden | 5,834,784 | heaviest test scene |

### 3.2 Splat rendering

- Instanced quads: one quad per Gaussian, projected with the full EWA
  Jacobian (3D covariance -> 2D conic).
- `splat.vert`: EWA projection, tight axis-aligned quad, SH evaluation.
- `splat.frag`: conic falloff, opacity, alpha blending.
- Tight quad change: instead of using the major-axis 4-sigma radius for both
  axes, use exact per-axis extents `4*sqrt(cov_xx)`, `4*sqrt(cov_yy)`.
  No visual change; roughly halves overdraw.

### 3.3 Sorting

- Phase 1: CPU back-to-front sort -> 64-bit keys / index buffers per frame.
- Phase 2: GPU LSD radix sort:
  - `keygen.comp`: view-space depth key + identity value; padding sorts last.
  - `radix_histogram/scan/scatter.comp`: 4 passes of 8 bits, stable.
  - Double-buffered per frame.
- Stability issue: the first scatter used shared `atomicAdd`, so equal keys
  could permute per frame -> visible flicker.
- Fix: deterministic stable scatter. Tried a per-thread histogram (4-bit
  radix, 8 passes) -> flicker fixed but 2.5x slower; then implemented
  CUB-style **warp-match ranking** (atomicOr match masks + popcount,
  warp-major layout) -> stable again at 4x8-bit speed.
- Idle-skip: if the camera/scene/settings did not change, reuse the previous
  sort instead of rebuilding it.
- `--noidle` forces a rebuild every frame for honest profiling.

### 3.4 Culling and indirect draw

- `keygen.comp` frustum-culls Gaussians behind/outside the view and atomically
  counts visible ones.
- `build_indirect.comp` turns the count into a `VkDrawIndirectCommand`.
- `RasterizePass` draws only visible instances; no edge popping.
- Screen-space floater filter: drops splats whose projected radius exceeds
  `maxSplatFraction * min(viewport)`. VR default 0.5; desktop 0.2.
- Optional 3D filters (`--min-opacity`, `--max-world-splat`, `--max-distance`)
  default off because aggressive settings make sparse scenes patchy.

### 3.5 Tiled rasterizer (compute)

Pipeline:

```
project.comp -> bin.comp (tileKey, depth) -> radix sort
             -> tile ranges -> tile_raster.comp (16x16 tiles)
             -> composite.frag -> presentation target
```

- `tile_bin.comp`: duplicate each Gaussian into every tile its ellipse
  overlaps; key = (tileID, depth).
- Reuses the same `RadixSorter` as the depth sort.
- `tile_raster.comp`: 16x16 tiles, shared-memory staging, front-to-back
  per-pixel early-out; stops blending when alpha is saturated.
- `composite.frag`: resolve the offscreen image to the swapchain.

Result: raster time 6.3 -> 4.6 ms on room, but binning inflated the sort
(~4x entries), so total was comparable or slower for these views
(current: room instanced 7.85 ms vs tiled 10.53 ms with forced sort). The honest
lesson: early-out only pays when overdraw is the bottleneck; here sorting is.

### 3.6 Presentation abstraction (the key to VR)

- `IPresentation` supplies `PresentFrame`:
  - up to `kMaxViews` targets (2 for VR)
  - per-view camera UBO, image/view, extent, render area, clear flag
- `DesktopPresentation`: one swapchain image normally; two side-by-side views
  in `--stereo-test`.
- Renderer loops views; background/splats recorded per view; depth sort built
  once from the centre view and reused; each distinct target image transitioned
  once.
- Passes know nothing about window vs HMD; they only see `RenderContext` /
  `PassContext`.
- The tiled path is single-view; multi-view falls back to the instanced path.

### 3.7 OpenXR / WiVRn

- `XrContext`: instance, system, session, LOCAL reference space, event/state
  handling, and the Vulkan instance/physical device/device created through
  `XR_KHR_vulkan_enable2`.
- `XrSwapchain`: per-eye colour swapchain + image views.
- `XrPresentation`:
  - `xrWaitFrame` / `xrBeginFrame`
  - acquire both eye images
  - `xrLocateViews` for per-eye asymmetric projections and poses
  - render into each eye image
  - `xrEndFrame` with an `XrCompositionLayerProjection`.
- Starting view: leveled from the average camera up; `--vr-world-scale`
  default 1.4 to compensate for arbitrary 3DGS units.
- Controllers: right **A** toggles the foveation debug overlay; right **B**
  takes a dual-eye screenshot.
- Refresh: `XR_FB_display_refresh_rate`, enumerate rates, request 72 Hz.

### 3.8 Fragment shading rate + fixed foveation

- `VK_KHR_fragment_shading_rate` enabled on the OpenXR-created device when
  supported.
- Uniform VRS: `--vr-shading-rate 2|4` applies 2x2/4x4 to all splat fragments.
- Fixed foveation: per-eye `R8_UINT` rate maps:
  - 16x16 texel granularity (driver max)
  - 1x1 in the centre, 2x2 in the ring, 4x4 outside
  - centred on each eye's optical axis from its projection
  - map values: 1x1 = 0, 2x2 = 5, 4x4 = 10
- `FoveationDebugPass`: tints classes green/yellow/red, quantized to the rate
  texels; toggled by **A**.
- Fixed fovea, not eye-tracked. `--vr-foveation-size` (default 0.40) and
  `--vr-foveation-ring` (default 0.32).

### 3.9 Foveated post-process blur

- When foveation+blur is enabled, render the scene into per-eye offscreen
  targets, then run a fullscreen pass into the eye image.
- 5x5 Gaussian (1-4-6-4-1 separable weights) mixed with the sharp sample by a
  smoothstep mask:
  - start = `foveaRadius - 0.08` (overlap the VRS class boundary)
  - end = `foveaRadius + ringWidth`
  - fovea sharp, ring lerps, periphery fully blurred.
- The 5x5 kernel covers 4x4 VRS blocks; a 3x3 kernel was too small for 4x4.
- Debug overlay is drawn after the blur so class colours stay crisp.
- `--vr-foveation-blur off` for A/B comparison.
- Cost: ~0.25-0.5 ms at 0.5 scale; ~1 ms at 1.0.

### 3.10 Refresh rate and render resolution

- VR requests 72 Hz by default when foveation is on; runtime rates are
  `[72, 80, 90, 120]`. `--vr-refresh 72|90|0` overrides.
- Render resolution = fraction of the runtime's recommended per-eye extent
  (`2476x2594` on the Quest runtime).
- Default: **0.75 when foveation is on** (`1856x1944`), **0.5 without**
  foveation (full-rate 0.75 is ~35 ms).
- `--vr-resolution-scale` overrides (clamped 0.25-2.0).
- Screenshots: both eyes side by side in one PNG by default; `--screenshot-eye
  0|1` for one eye.

---

## 4. Performance

### 4.1 Desktop (1600x900, current code)

Measured with `--profile` (91 frames). `--noidle` forces the sort every frame;
idle-skip reuses it for a static camera.

| Scene | Path | Keygen | Radix | Raster | Total | Approx FPS |
|---|---|---:|---:|---:|---:|---:|
| room | instanced, noidle | 0.47 | 0.89 | 6.45 | **7.85 ms** | ~127 |
| room | instanced, idle (earlier run) | 0.01 | 0.02 | 6.53 | **6.61 ms** | ~151 |
| room | tiled, noidle | 0.79 | 3.57 | 4.93 | **10.53 ms** | ~95 |
| garden | instanced, noidle | 2.71 | 5.92 | 11.71 | **20.40 ms** | ~49 |
| garden | tiled, noidle | 1.43 | 7.84 | 9.76 | **24.85 ms** | ~40 |
| playroom | instanced, idle (earlier run) | 0.01 | 0.02 | 5.32 | **5.41 ms** | ~185 |
| drjohnson | instanced, idle (earlier run) | 0.02 | 0.03 | 5.07 | **5.17 ms** | ~193 |

Note: the idle numbers reuse the sort; use `--noidle` for the true per-frame
cost when the camera moves. Runs vary by roughly 5-10% with GPU clocks/thermal;
compare pairs measured in the same session.

#### All 13 scenes (1600x900, `--noidle`)

Saved logs: `presentation/captures/perf/desktop_<scene>_<mode>.log`.
Summary table/charts: `desktop_all_scenes.md/.csv`,
`desktop_scene_totals.png`, `desktop_scene_raster.png`,
`desktop_scene_breakdown.png`.

| Scene | Inst total | Inst FPS | Tiled total | Tiled FPS | Tiled vs inst | Inst raster | Tiled tiles | Tile saving |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bicycle | 20.09 | 50 | 26.41 | 38 | +31.5% | 11.35 | 9.72 | 1.63 ms |
| garden | 16.67 | 60 | 21.31 | 47 | +27.8% | 9.89 | 9.22 | 0.67 ms |
| stump | 13.96 | 72 | 16.52 | 61 | +18.3% | 8.96 | 6.77 | 2.19 ms |
| kitchen | 11.54 | 87 | 13.76 | 73 | +19.2% | 9.48 | 5.99 | 3.49 ms |
| truck | 9.68 | 103 | 11.95 | 84 | +23.5% | 7.36 | 5.02 | 2.34 ms |
| treehill | 9.47 | 106 | 11.22 | 89 | +18.5% | 6.19 | 4.43 | 1.76 ms |
| counter | 8.65 | 116 | 10.07 | 99 | +16.4% | 7.54 | 4.35 | 3.19 ms |
| room | 8.40 | 119 | 10.63 | 94 | +26.5% | 6.92 | 4.97 | 1.95 ms |
| flowers | 7.75 | 129 | 9.88 | 101 | +27.5% | 4.65 | 4.02 | 0.63 ms |
| drjohnson | 7.57 | 132 | 8.44 | 118 | +11.5% | 5.03 | 3.84 | 1.19 ms |
| train | 7.54 | 133 | 8.15 | 123 | +8.1% | 6.53 | 3.19 | 3.34 ms |
| playroom | 7.36 | 136 | 9.62 | 104 | +30.7% | 5.33 | 4.35 | 0.98 ms |
| bonsai | 4.52 | 221 | 6.55 | 153 | +44.9% | 3.50 | 2.61 | 0.89 ms |

- Tiled tile raster is faster than instanced raster in **all 13 scenes**:
  mean saving 1.87 ms, median 1.76 ms.
- Tiled total is slower in **all 13 scenes**: mean overhead +23.4%, median
  +23.5%. Closest: train +8.1%, drjohnson +11.5%, counter +16.4%. Worst:
  bonsai +44.9%, bicycle +31.5%, playroom +30.7%.
- The tiled path duplicates each Gaussian into every overlapping tile, so
  project/bin/sort entry counts are much larger than the instanced depth sort;
  that overhead cancels the tile-raster win on these views.


### 4.2 Historical optimization deltas (from commit messages)

| Change | Before -> after |
|---|---|
| CPU sort -> GPU LSD radix (room) | ~85 ms (11 FPS) -> ~20 ms (51 FPS) |
| Tight quad raster (room) | 15.8 -> 7.0 ms |
| Tight quad raster (garden) | 21.9 -> 12.0 ms |
| Frustum culling (playroom) | 10.2 -> 6.8 ms |
| Frustum culling (drjohnson) | 12.9 -> 7.1 ms |
| Frustum culling (garden) | 21.0 -> 15.9 ms |
| Warp-match stable radix (room) | 2.50 -> 0.87 ms |
| Warp-match stable radix (garden) | 9.73 -> 4.91 ms |
| Idle-skip (garden, instanced) | 15.98 -> 11.84 ms |
| Idle-skip (garden, tiled) | 20.04 -> 9.02 ms |
| Tiled raster (room) | 6.3 -> 4.6 ms (total not better) |

### 4.3 VR (room, per eye, foveation 4, 5x5 blur, 72 Hz)

The 72 Hz budget is **13.9 ms** (90 Hz = 11.1 ms).

| Render scale | Per-eye extent | Total GPU | Notes |
|---:|---:|---:|---|
| 0.50 | 1238x1296 | **12.3 ms** | reprojection-free 72 Hz |
| 0.55 | 1360x1426 | 14.2 ms | slightly over the 13.9 ms budget |
| 0.60 | 1484x1556 | 15.4 ms | default fovea; smaller fovea 0.25 gives 13.5-13.8 |
| **0.75 (default)** | **1856x1944** | **20.1 ms** | sharper; relies on reprojection |
| 1.00 | 2476x2594 | 28.9 ms | best image; ~35 FPS + reprojection |

Foveation comparison at 0.5 scale (1238x1296/eye, latest logs):

| Mode | Total GPU | Raster |
|---|---:|---:|
| No foveation (1x1 everywhere) | 19.1 ms | 17.4 |
| Uniform 2x2 VRS | 11.2 ms | 9.5 |
| Uniform 4x4 VRS | 8.3 ms | 6.9 |
| Fixed foveation 2 (1x1 centre + 2x2 outer) | 13.3 ms | 11.5 |
| Fixed foveation 4 (1x1 + 2x2 ring + 4x4 outer) | 12.3 ms | 10.5 |

Fixed foveation 4 removes ~36% of the frame time versus no foveation in the
latest runs (the earlier run measured 21.2 -> 12.5 ms, ~41%); uniform 4x4 is
faster still but blurs everything. At the 0.75 default, foveation 4 is 20.1 ms
and foveation 2 is 22.0 ms.

Other scenes at the 0.75 default:

| Scene | Total GPU |
|---|---:|
| counter | 14.8 ms |
| bonsai | 6.9 ms |
| room | 20.1 ms |

### 4.4 Key comparison for the talk

- Desktop room: **~11 FPS** (Phase 1 CPU sort) -> **~51 FPS** (GPU sort) ->
  **~126 FPS** (current, forced sort) / ~151 FPS idle.
- VR room: **19.1 ms** no foveation -> **12.3 ms** with fixed foveation 4 at
  0.5 scale (-36%; earlier run 21.2 -> 12.5, -41%).
- At the shipped 0.75 default, room is ~20 ms but visually much sharper:
  2.25x the pixels of 0.5 and 1.5x smaller VRS blocks angularly.

---

## 5. Bugs fixed (good story material)

| Bug | Cause | Fix |
|---|---|---|
| Wrong initial camera orientation | COLMAP stores y-down; world up is negative camera Y | Use `-column(1)` as up |
| Orbit direction inverted | Orbit pole hardcoded to +Y, lookAt used scene up | Build basis from scene up |
| Rainbow fringing / scrambled colour | INRIA `f_rest` is channel-major, not coefficient-major | Correct indexing |
| Washed-out colours | sRGB swapchain re-encoded/blended in linear | Use `B8G8R8A8_UNORM` |
| Sort flicker | Atomic scatter permuted equal keys per frame | Stable scatter + warp-match rank |
| Tiled path flicker | Same unstable scatter, worse with duplicated tile entries | Same determinism fix |
| Black VR stream | Eye images not created with sampled usage | Add `XR_SWAPCHAIN_USAGE_SAMPLED_BIT` |
| VR busy-loop freeze | Spinning when `shouldRender=0` | Sleep when not rendering |
| VR starts tilted | Leveling from a single camera's up | Average up over all cameras |
| `counter`/`bonsai` loaded room | Bare names fell back silently | Resolve names against `assets/models` |
| Foveation artifacts in the centre | Shrinking fovea to 0.25 moves 4x4 into the main view | Keep 0.40/0.32; tune resolution instead |

---

## 6. Vulkan features used (the "interest in Vulkan" thread)

| Phase | Vulkan features |
|---|---|
| Scaffold | Instance/device/swapchain, VMA, dynamic rendering, descriptor sets, ImGui |
| Splat viewer | Graphics pipeline, instancing, alpha blending, vertex/fragment shaders, push constants |
| SH | Vertex-shader SH evaluation, UBOs |
| GPU sort | Compute shaders, storage buffers, shared memory, atomics, barriers, multi-pass dispatch |
| Culling | Compute keygen, atomic counters, indirect draw |
| Profiling | `VkQueryPool` timestamps |
| Tiled | Compute project/bin/raster, shared-memory staging, early-out, reuse of radix sorter |
| VR | `XR_KHR_vulkan_enable2`, external `VkDevice`, multi-view render loop, per-eye swapchains, stereo projection layer |
| Foveation | `VK_KHR_fragment_shading_rate`, rate-map attachments, dynamic rendering, post-process offscreen targets |
| Refresh | `XR_FB_display_refresh_rate` |

---

## 7. How the headset connection works (high level)

```
PC                                                                 Quest 3
┌──────────────────────────────┐                                  ┌─────────────────────┐
│ 3DGS renderer (Vulkan)       │                                  │ WiVRn client        │
│   ↓ OpenXR API               │   video stream (USB / Wi-Fi)     │   ↓ decode          │
│ WiVRn OpenXR runtime         │ ───────────────────────────────▶ │ Quest compositor    │
│ (Monado-based)               │                                  │   ↓ lens distortion │
│   ↓ encode eye textures      │ ◀─────────────────────────────── │ display             │
│                              │   head/controller tracking       │                     │
└──────────────────────────────┘                                  └─────────────────────┘
```

- The app is a **PCVR** app; it does not run on the headset.
- OpenXR gives the app per-eye swapchain images. The app renders into them and
  submits a stereo projection layer.
- WiVRn (Monado) acts as the OpenXR runtime: it composites/encodes the eye
  textures into a low-latency video stream and sends them to the Quest client.
- The Quest client decodes, applies lens distortion / reprojection, and shows
  them. Tracking and controller input travel back over the same link.
- USB route: `adb reverse tcp:9757 tcp:9757`; the runtime URL is
  `wivrn+tcp://127.0.0.1:9757`. Wi-Fi works too.
- Because the runtime owns presentation, the app can miss a frame and the
  runtime can reproject; this is why 0.75/1.0 scale still feels usable even
  above the 13.9 ms budget.

---

## 8. Current defaults / commands

```bash
# Desktop (1600x900)
./scripts/run-vr.sh room                       # VR, no foveation (0.5 scale)
./scripts/run-vr.sh room --vr-foveation 4      # recommended: 0.75, blur, 72 Hz

# Explicit resolution
./scripts/run-vr.sh room --vr-foveation 4 --vr-resolution-scale 0.5   # 72 Hz-safe
./scripts/run-vr.sh room --vr-foveation 4 --vr-resolution-scale 1.0   # quality

# Screenshots (both eyes by default)
./scripts/run-vr.sh room --screenshot --vr-foveation 4
./scripts/run-vr.sh room --screenshot --screenshot-eye 1 --vr-foveation 4

# Performance
./scripts/run-vr.sh room --vr-foveation 4 --frames 90
./build/3DGSRendererVulkan room --profile --noidle          # desktop
```

Controller: right **A** toggles the foveation debug overlay; right **B** takes a
dual-eye screenshot.

---

## 9. What is left / future work

- Locomotion (the VR base viewpoint is currently fixed).
- Dynamic resolution: high scale when still, lower when moving, targeting
  13.9 ms.
- Multi-view / multiview for the tiled path.
- fp16 Gaussian storage; front-to-back early-out in the instanced path.
- Temporal AA/jitter to reduce motion shimmer further.
- In-VR settings/UI (currently CLI flags).
- Built-in video capture / camera paths for repeatable demos.
- Possible training of an own scene (Phase 3 in the roadmap).

---

## 10. Presentation soundbites

- "I reimplemented the whole 3DGS pipeline in Vulkan, from PLY parsing to EWA
  splatting, my own GPU radix sort and a compute tile rasterizer."
- "The biggest single win was moving the depth sort to the GPU: 11 -> 51 FPS."
- "Tight quads and frustum culling roughly halved raster time."
- "The tiled rasterizer cut raster time but lost on total time -- a good lesson
  in measuring the whole pipeline."
- "The presentation abstraction let me add OpenXR without rewriting the
  renderer."
- "Fixed foveation via VRS took VR from 19 ms to 12.3 ms at the same sharp
  centre."
- "The 5x5 blur hides the 4x4 VRS blocks; a 3x3 kernel was too small."
- "At 0.75 scale the image is much sharper, and the runtime's reprojection
  absorbs the missed 72 Hz deadline."
