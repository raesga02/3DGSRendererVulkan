# 3DGS Renderer (Vulkan)

A from-scratch **3D Gaussian Splatting (3DGS)** visualizer written in C++20 and
Vulkan 1.3, built as a prototype for the **TDT16_XR** theory module.

The goal is a fast, portable desktop splat viewer that loads *already trained*
INRIA `.ply` scenes and lets you fly around them in real time. The rendering
layer is deliberately decoupled from presentation, so an OpenXR (VR/MR) path can
be added on top without rewriting the renderer.

> Status: **Phase 2 (in progress)** — Phase 1 viewer plus degree-3 view-dependent
> colour and a GPU radix sort. The room scene went from ~11 FPS to ~53 FPS.
> Remaining Phase 2 ideas: front-to-back early-out, fp16 storage, culling.

---

## Why this project

3D Gaussian Splatting is an explicit, point-based radiance-field representation:
a scene is a cloud of anisotropic 3D Gaussians, each with position, scale,
rotation, opacity and spherical-harmonics (SH) colour. Rendering them means
projecting each Gaussian's 3D covariance into screen space and alpha-compositing
them in depth order. The interesting graphics work is:

- the **EWA projection** (3D covariance → 2D conic),
- the **depth sort** required for correct transparency,
- and efficient **rasterization** of millions of translucent primitives.

The academic reference implementations are CUDA-only. Reimplementing the
pipeline in Vulkan is both a strong learning exercise and a useful, cross-vendor
demonstration. The course's "understanding a concept" objective is served by an
interactive visualisation of the pipeline stages (Phase 3).

---

## Current status (Phase 2)

Implemented:

- CMake build with Vulkan SDK + shader compilation (`glslangValidator`).
- GLFW window, `vkb::Instance`/device selection, VMA allocator, swapchain.
- `IPresentation` abstraction with a working `DesktopPresentation`.
- Pass framework (`Pass`, `RenderContext`, `PassContext`).
- `BackgroundPass` (fullscreen gradient) and `GUIPass` (Dear ImGui overlay).
- **`PlyLoader`**: header-driven INRIA `.ply` parser (SH degree detected);
  `CameraLoader` reads `cameras.json` for a correct initial pose.
- **`GaussianScene`**: pre-activation (sigmoid/exp/quaternion), CPU-precomputed
  3D covariance, packed GPU buffer with all 48 SH coefficients.
- **`RasterizePass` + `splat.vert/.frag`**: instanced-quad EWA projection with
  the full 3D→2D covariance Jacobian and per-Gaussian conic.
- **Degree-3 spherical harmonics** in the vertex shader (view-dependent colour).
- **`SortPass`**: GPU LSD radix sort (keygen + 4×8-bit stable histogram/scan/
  scatter), double-buffered per frame.
- Orbit + fly cameras auto-framed from the scene or the first training camera.
- Floater filter, splat-scale and SH-degree controls; `F12` screenshots.
- **Estimated GPU memory** readout in the GUI (total + scene/sort/frame/swapchain
  breakdown) and logged on scene load; per-scene estimates shown in the picker.
  Computed from requested buffer sizes (no driver queries).
- **Colour-space correct output:** a non-sRGB (`B8G8R8A8_UNORM`) swapchain so the
  SH colours pass through and blend in the training image's space, matching the
  3DGS reference (an sRGB target double-encodes and blends in linear space,
  washing colours out).

Deferred: offscreen linear-space compositing + tonemap (would change the colour
space relative to the 3DGS reference; revisit alongside HDR/exposure), plus
front-to-back early-out and fp16 packing.

### Controls

| Input | Action |
|---|---|
| Left-drag | Orbit rotate (`Orbit` mode) |
| Scroll | Zoom (`Orbit` mode) |
| Right-drag | Look around (`Fly` mode) |
| `W A S D` / `Q E` / `Shift` | Move / down-up / fast (`Fly` mode) |
| `Tab` | Toggle Orbit ↔ Fly |
| `H` | Toggle GUI |
| `F12` | Save screenshot |
| `Esc` | Quit |

Run with a specific scene:

```bash
./build/3DGSRendererVulkan assets/models/counter/point_cloud.ply
```

---

## Architecture

```
3DGSRendererVulkan/
├── CMakeLists.txt              # Vulkan, shader compile, submodules, asset copy
├── external/                   # git submodules (pinned)
│   └── glfw/ glm/ fmt/ imgui/ vma/ vk-bootstrap/ stb/
├── assets/
│   └── models/                  # downloaded .ply scenes (git-ignored)
├── shaders/
│   ├── common/                  # shared GLSL (Gaussian layout, SH helpers)
│   ├── background.vert/.frag    # gradient background
│   ├── splat.vert/.frag         # instanced-quad EWA rasterization + SH eval
│   ├── keygen.comp              # depth keys + identity values
│   ├── radix_histogram.comp     # \
│   ├── radix_scan.comp          #  } GPU LSD radix sort
│   ├── radix_scatter.comp       # /
│   └── composite.vert/.frag     # (deferred) offscreen → target, tonemap
├── scripts/
│   ├── download_models.sh       # fetch pretrained INRIA scenes
│   └── download_models.py       # HTTP-range extraction of one scene
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Application.*        # window, main loop, input, lifecycle
│   │   ├── CameraController.*   # orbit + fly cameras
│   │   ├── InputState.hpp       # desktop + XR unified input snapshot
│   │   ├── IPresentation.hpp    # "where pixels go" interface (desktop/XR)
│   │   └── DesktopPresentation.*# swapchain + per-frame sync (GLFW/Vulkan)
│   ├── core/
│   │   ├── VulkanContext.*      # VkInstance + surface
│   │   ├── Device.*             # physical/logical device, queues, one-shot cmds
│   │   └── Swapchain.*          # vkb swapchain wrapper
│   ├── gpu/
│   │   ├── Allocator.*          # VMA
│   │   ├── Buffer.*             # VMA-backed buffer
│   │   └── Image.*              # VMA-backed image + layout tracking
│   ├── io/
│   │   ├── PlyLoader.*          # INRIA .ply parser
│   │   ├── CameraLoader.*       # cameras.json (initial pose)
│   │   └── GpuSplatData.hpp     # packed GPU Gaussian (std430)
│   ├── scene/
│   │   ├── GaussianData.hpp     # POD: pos, scale, rot, opacity, SH
│   │   ├── Camera.*             # view/projection
│   │   └── GaussianScene.*      # Gaussians, metadata, GPU buffers
│   ├── renderer/
│   │   ├── Renderer.*           # frame orchestration, pass scheduling
│   │   ├── RenderContext.hpp    # shared services + target info
│   │   ├── SceneState.hpp       # UI-adjustable state
│   │   ├── RenderStats.hpp      # fps / frame time
│   │   └── passes/
│   │       ├── Pass.hpp         # base pass interface
│   │       ├── BackgroundPass.* # gradient background
│   │       ├── GUIPass.*        # Dear ImGui overlay
│   │       ├── RasterizePass.*  # instanced-quad splatting + SH
│   │       ├── SortPass.*       # GPU radix sort
│   │       └── DebugPass.*      # (Phase 3) pipeline visualisations
│   ├── xr/                      # Phase 4: XrContext / XrSwapchain / XrInput
│   └── utils/
│       ├── Shader.*             # SPIR-V module loading
│       ├── VulkanUtils.hpp      # barriers / layout transitions
│       ├── Math.hpp             # quat→matrix, cov3D
│       └── Log.hpp              # fmt logging
└── README.md
```

### Key design decisions

- **Presentation is abstracted away from rendering.** `Renderer` draws into a
  `PresentTarget` supplied by an `IPresentation`. Desktop uses a swapchain;
  Phase 4 will add an `XrPresentation` that submits stereo layers to OpenXR.
- **Passes know nothing about the window.** They only see `RenderContext` and
  `PassContext`, so they are reused verbatim across desktop and VR.
- **Data is separated from rendering.** `io/` produces CPU Gaussians;
  `scene/` owns them and their GPU buffers; `renderer/` only consumes them.
- **Everything is a `Pass`.** New techniques (tile rasterizer, sort-free
  variants, debug views) slot in without touching the frame loop.

---

## Roadmap

| Phase | Deliverable | Highlights |
|:-----:|-------------|------------|
| **0** | ✅ Scaffold | Build system, Vulkan core, pass framework, ImGui, background |
| **1** | ✅ Desktop splat viewer | PLY load, EWA projection, instanced-quad splats, CPU depth sort, orbit/fly cameras |
| **2** | ✅ Real-time (partial) | ✅ Own GPU radix sort, ✅ SH degree 3; early-out / fp16 / culling pending |
| **3** | Own splat + teaching layer | Capture + train own `.ply`, debug/explainer views, camera-path recording |
| **4** | VR (stretch) | OpenXR PCVR against Quest 3/Oculus, stereo rendering, controller locomotion |
| **5** | Polish | Scene switching, performance stats, report/slides material |

### Decisions already made

- **Desktop is the primary deliverable; VR is a modular stretch goal.**
- **VR target:** PCVR via **OpenXR** against the lab's Oculus / Meta Quest 3
  (not standalone Android). *Linux caveat:* Meta's PC runtime is Windows-only,
  so on this machine Quest 3 PCVR would go through **WiVRn or ALVR → Monado**;
  a Windows lab PC is the simpler fallback. Runtimes will be installed in Phase 4.
- **Rasterization:** instanced-quad billboards + hardware alpha blending first,
  behind the `RasterizePass` interface so a tile-based compute path can replace it.
- **Sorting:** implement the radix sort ourselves (bitonic first as a
  correctness checkpoint, then 4×8-bit LSD radix).

### Memory budget

The dev GPU has **8 GB VRAM**. Degree-3 SH is ~200 B/Gaussian, so a 5 M-Gaussian
Mip-NeRF360 scene needs ~1.3 GB for SH alone. Phase 1 starts with DC-only SH and
the smaller scenes (`room`, `counter`, `kitchen`, `truck`, `train`); Phase 2 adds
fp16 storage and degree-3 support.

---

## Building

Requirements:

- Vulkan 1.3+ SDK (`glslangValidator` on `PATH` or under `$VULKAN_SDK`).
- CMake ≥ 3.20 and a C++20 compiler (tested with GCC 13).
- Linux (X11/Wayland), Windows or macOS via the same submodules.

```bash
git clone --recursive <repo-url>
cd 3DGSRendererVulkan

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

./build/3DGSRendererVulkan
```

See the **Controls** table above. Pass a scene name (e.g. `counter`) or a
specific `.ply` path to load it.

### VR (OpenXR / WiVRn)

The renderer can drive an OpenXR runtime directly. This has been tested with
**WiVRn** (PCVR streaming to a Meta Quest 3), but any Vulkan OpenXR runtime
should work.

Build prerequisites:

- OpenXR-SDK-Source built with the loader (headers in `build/include`, loader
  in `build/src/loader`), or a system OpenXR package.
- If the SDK is not at the default discoverable location, configure with
  `-DOPENXR_SDK_ROOT=/path/to/openxr-sdk`. `-DENABLE_OPENXR=OFF` builds the
  desktop-only renderer.

Run:

```bash
flatpak run --user --command=wivrn-server io.github.wivrn.wivrn   # if needed
./scripts/run-vr.sh room
```

- The first argument is the scene; remaining flags are passed to the renderer
  (for example `--nosort`, `--frames 120`, `--screenshot`,
  `--vr-world-scale 1.4`).
- **World scale:** 3DGS reconstructions have arbitrary units. The default VR
  world scale is `1.4` (scene scaled about the starting viewpoint). Larger
  values make the room feel larger; use `--vr-world-scale <s>` to tune it.
- The VR starting view is **leveled**: the scene camera's yaw is kept, but its
  pitch/roll is discarded so looking straight ahead does not inherit the
  dataset camera's downward tilt.
- **Floater culling:** the screen-space filter is on by default (`max splat
  fraction 0.5` in VR, 0.2 on desktop). Use `--max-splat-fraction <f>` to tune
  it, or `--no-cull` to disable all floater filters. Optional 3D filters
  (`--min-opacity <f>`, `--max-world-splat <fraction of scene radius>`,
  `--max-distance <factor of scene radius>`) are available but default to 0
  (off); aggressive settings can make sparse reconstructions look patchy.
- **Uniform VRS:** `--vr-shading-rate 1|2|4` enables `VK_KHR_fragment_shading_rate`
  at 2x2 or 4x4 for all splat fragments (default 1 = off). Measured on `room`
  at 1238x1296 per eye: 21.1 ms total (raster 19.3) at 1x1, 12.2 ms (raster
  10.3) at 2x2, and 10.8 ms (raster 9.0) at 4x4. The uniform rates blur the
  whole image.
- **Fixed foveation:** `--vr-foveation 2|4` builds a per-eye rate map with a
  sharp 1x1 centre and 2x2/4x4 periphery, centred on each eye's optical axis.
  The sharp region is fixed to the forward direction (not eye-tracked).
  `--vr-foveation-size <fraction>` controls the 1x1 radius (default 0.40) and
  `--vr-foveation-ring <fraction>` the width of the 2x2 ring (default 0.32).
  At 1238x1296/eye this is roughly a 500 px fovea diameter and a 200 px
  intermediate ring; everything outside is 4x4 for `--vr-foveation 4`.
  Measured on `room` at the defaults: ~13.7 ms total for `2` and ~12.5 ms for
  `4`, versus 12.2 ms uniform 2x2 and 21.2 ms baseline.
- **Foveation blur:** on by default when foveation is enabled. The scene is
  rendered into per-eye offscreen targets, then a fullscreen pass mixes a 5x5
  Gaussian blur over the sharp sample using a smoothstep mask from
  `foveaRadius - 0.08` to `foveaRadius + ringWidth`. The fovea stays sharp,
  the intermediate ring lerps toward the blur, the periphery is fully blurred,
  and the mask overlaps the 1x1/2x2 VRS boundary to hide the class transition.
  `--vr-foveation-blur off` disables it for comparison. Measured on `room` at
  1238x1296/eye: 12.5 ms with `4` + blur (12.1 ms without) and 13.7 ms with
  `2` + blur (12.8 ms without).
- **Refresh rate:** VR defaults to requesting **72 Hz** when foveation is
  enabled; `--vr-refresh 72|90|0` overrides it (`0` keeps the runtime default).
  At 72 Hz the 12.5–13.7 ms frames fit the 13.9 ms budget instead of relying
  on reprojection at 90 Hz.
- **Foveation debug view:** `--vr-foveation-debug` (or right controller **A**)
  tints the splat image by rate-map class — green = 1x1, yellow = 2x2, red =
  4x4 — quantized to the 16x16 rate texels and centred on each eye's optical
  axis. It is drawn after the blur, so the classes stay crisp.
- **Screenshots:** right controller **B** saves a screenshot to
  `build/saves/screenshot_NNN.png`. By default the PNG is **both eyes
  side by side** (two per-eye images in one file, e.g. 2476x1296 in VR).
  `--screenshot` is the one-shot at launch; `--screenshot-eye 0|1` captures a
  single eye instead. These capture the rendered eye image, not the
  composited/lens-distorted headset view. For the true headset view, use the
  Quest's system screenshot shortcut (Meta/Oculus button + trigger) and pull
  it with adb from `/sdcard/Oculus/Screenshots` or
  `/sdcard/Pictures/Screenshots`.
- The runtime manifest is taken from the WiVRn Flatpak. Override with
  `XR_RUNTIME_JSON=<manifest>`.
- Per-eye resolution starts at 50% of the runtime's recommendation. There is
  no in-VR GUI yet; head tracking is active and the base viewpoint is the
  scene's initial camera. Locomotion is the next milestone.
- Stop with `Ctrl+C` or by ending the streaming session in the headset.

---

## Datasets

Pretrained scenes are the official **INRIA 3DGS** models (`.ply`,
`iteration_30000`):

- Full 13-scene archive (~14.66 GB):
  <https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/datasets/pretrained/models.zip>
- Scenes: `bicycle flowers garden stump treehill room counter kitchen bonsai
  truck train drjohnson playroom`.

Download individual scenes (HTTP-range extraction, one scene at a time):

```bash
./scripts/download_models.sh --list          # show available scenes
./scripts/download_models.sh room            # ~350-700 MB
./scripts/download_models.sh truck counter
```

Other sources:

- `nerfstudio-project/gsplat` paper-scene downloader (source of the range logic).
- `antimatter15/splat` — compact `.splat` scenes and a WebGL viewer.
- PlayCanvas **SuperSplat** (<https://superspl.at>) — sample scenes + browser editor.
- **Polycam** / **Luma AI** — capture on a phone and export `.ply`/`.splat`.
- Source captures for retraining: Tanks & Temples / Deep Blending SfM data
  (<https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/datasets/input/tandt_db.zip>).

**Creating your own splat** (Phase 3): capture 1–2 min of photos/video, run
**COLMAP**/GLOMAP for SfM, then train with **gsplat/nerfstudio**, **Postshot**
(Windows GUI) or **OpenSplat**. Luma AI on a phone is the fastest path.

---

## References

- Kerbl et al., *3D Gaussian Splatting for Real-Time Radiance Field Rendering*,
  SIGGRAPH 2023.
- `graphdeco-inria/gaussian-splatting` (reference implementation).
- `shg8/3DGS.cpp` — cross-platform Vulkan compute 3DGS renderer (LGPL).
- `nvpro-samples/vk_gaussian_splatting` — Vulkan 3DGS testbed (Apache-2.0).

Third-party libraries are vendored as submodules and keep their original
licenses (`glfw`, `glm`, `fmt`, `imgui`, `vk-bootstrap`, `VMA`, `stb`).
