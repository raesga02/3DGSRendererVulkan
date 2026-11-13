# 15-minute slide plan

**Talk title:** *From 3D Gaussian Splatting to foveated VR: reimplementing the
pipeline in Vulkan*

**Target:** 12 content slides, 13:45 of speaking, ~1:15 buffer/Q&A. Hard stop at
15:00. Focus: implementation roadtrip, Vulkan, measured wins, visuals.

**Narrative arc:** base 3DGS -> first Vulkan viewer -> GPU sort -> desktop
optimizations -> tiled experiment -> VR bridge (OpenXR + streaming) -> VR
fixes -> foveation -> results -> lessons.

---

## Slide-by-slide

### 1. Title (0:30)

- **Title:** Reimplementing 3D Gaussian Splatting in Vulkan — from desktop
  viewer to foveated VR
- Subtitle: TDT16_XR project / your name / date
- **Visual:** full-bleed dual-eye room screenshot or a Quest VR clip.
- Say: one sentence on what the audience will see: a from-scratch renderer,
  three optimization stages, and a VR headset.

---

### 2. What 3DGS is, and why Vulkan (1:00)

- **Key message:** 3DGS is an explicit point-based radiance field; the paper's
  reference is CUDA-only, so a Vulkan reimplementation is a real graphics
  engineering exercise.
- **Visual:** diagram: 3D anisotropic Gaussian -> EWA projection to a 2D
  ellipse -> depth sort -> alpha blend; inset close-up of the room.
- Talking points:
  - Millions of Gaussians, each with position/scale/rotation/opacity/SH colour.
  - Project 3D covariance to 2D, sort back-to-front, blend.
  - My goal: same visual result, cross-vendor, explicit Vulkan pipeline.
  - No engine: I own instance/device/swapchain/passes.

---

### 3. Architecture: how it is put together (1:00)

- **Key message:** data, renderer and presentation are separated; the
  `IPresentation` abstraction later makes VR a drop-in.
- **Visual:** box diagram:
  `PLY/cameras -> scene -> passes (background/sort/raster/gui) -> IPresentation
  -> DesktopSwapchain | OpenXR`.
- Talking points:
  - C++20 + Vulkan 1.3, VMA, GLFW, Dear ImGui.
  - Pass framework: every stage is a `Pass` with `RenderContext`/`PassContext`.
  - `IPresentation` supplies targets and per-view cameras.
  - This separation is the main reason the VR port was feasible.

---

### 4. Phase 1: the first splat viewer (1:15)

- **Key message:** a correct 3DGS viewer from scratch, but the CPU sort is the
  bottleneck.
- **Visual:** first desktop screenshot; 5-10 s orbit clip; optional wireframe
  or Gaussian-ellipse diagram.
- Talking points:
  - Header-driven INRIA PLY parser; SH degree detection.
  - Pre-activation (sigmoid/exp/quaternion), precomputed covariance,
    52 B/Gaussian GPU layout.
  - Instanced quads with the full EWA Jacobian, per-Gaussian conic, alpha
    blending.
  - CPU back-to-front sort; orbit/fly camera; `cameras.json` initial pose.
  - Bug story: COLMAP y-down orientation -> fixed initial camera and orbit
    basis.
  - Result: room ~11 FPS.

---

### 5. Phase 2: SH colour + GPU radix sort (1:15)

- **Key message:** view-dependent colour and the GPU sort transform the
  renderer.
- **Visual:** SH degree 0 vs degree 3 side-by-side (same view); radix sort
  pipeline diagram (keygen -> 4x hist/scan/scatter).
- Talking points:
  - Degree-3 SH: 48 coefficients, evaluated in the vertex shader.
  - Bug: INRIA `f_rest` is channel-major -> rainbow fringing; fixing the
    indexing made colours natural.
  - GPU LSD radix sort: 4 x 8-bit stable passes; keygen + histogram + scan +
    scatter; double-buffered.
  - Result: room ~85 ms (~11 FPS) -> ~20 ms (~51 FPS).

---

### 6. Desktop optimization pass (1:15)

- **Key message:** measure, then remove overdraw and unnecessary work.
- **Visual:** bar chart of ms before/after (tight quad, culling, stable sort,
  idle-skip); maybe a zoomed splat with the tight quad.
- Talking points:
  - Tight per-axis quad: exact `4*sqrt(cov_xx/yy)`; raster ~halved.
  - Frustum culling in keygen + atomic count + indirect draw.
  - Deterministic stable radix: flicker fixed; warp-match ranking brought the
    sort back to 0.87 ms on room.
  - Idle-skip: reuse the sort when the view is unchanged.
  - Current: room 7.9 ms forced sort (~126 FPS), 6.6 ms idle (~151 FPS) at
    1600x900.

---

### 7. Tiled rasterizer: the experiment that did not win (1:15)

- **Key message:** the tiled path is a good Vulkan/compute exercise, but the
  total pipeline matters more than one stage.
- **Visual:** tile pipeline diagram; `desktop_scene_totals.png` and
  `desktop_scene_raster.png`; `desktop_scene_breakdown.png` for bicycle/garden/
  room.
- Talking points:
  - Pipeline: project -> bin (tileKey, depth) -> radix sort -> 16x16 tile
    raster -> composite.
  - Shared-memory staging and per-pixel front-to-back early-out.
  - Across all 13 desktop scenes, tile raster is faster than instanced raster
    **everywhere** (mean saving 1.87 ms, median 1.76 ms).
  - But tiled total is slower **everywhere** (mean +23.4%; best train +8.1%,
    drjohnson +11.5%, counter +16.4%; worst bonsai +44.9%).
  - Room example: raster 6.92 -> 4.97 ms, but project/bin/sort overhead grows;
    total 10.63 vs 8.40 ms.
  - Why: the tiled path duplicates each Gaussian into every overlapping tile,
    so project/bin/sort entry counts are much larger than the instanced depth
    sort. The tile-raster win is cancelled by that overhead on these views.
  - Lesson: measure the whole pipeline; early-out pays only when overdraw
    dominates. The work still produced the reusable stable radix sorter and
    compute experience.

---

### 8. VR bridge: multi-view + OpenXR (1:30)

- **Key message:** the renderer did not change much; the presentation layer
  did.
- **Visual:** multi-view diagram (one renderer, two eye cameras/targets); the
  OpenXR frame loop; a photo/Clip of the Quest.
- Talking points:
  - `PresentFrame` now carries N views, per-view camera UBOs, targets and
    render areas.
  - Sort once from the centre view, reuse for both eyes.
  - `XrPresentation`: per-eye swapchains + `xrWaitFrame` / `xrBeginFrame` /
    `xrLocateViews` / `xrEndFrame` with a stereo projection layer.
  - `XR_KHR_vulkan_enable2` creates the Vulkan instance/device through the
    runtime.
  - Starting view leveled from average camera up; world scale 1.4.

---

### 9. Streaming + the VR bugs (1:00)

- **Key message:** PCVR is a streaming pipeline; the headset never runs the
  renderer.
- **Visual:** connection diagram (PC renderer -> OpenXR/WiVRn -> encode ->
  USB/Wi-Fi -> Quest decode -> compositor/lens; tracking back). A Quest
  recording clip is the payoff.
- Talking points:
  - WiVRn (Monado-based) is the OpenXR runtime; it encodes the eye textures
    and streams to the Quest client.
  - USB via `adb reverse tcp:9757`; Wi-Fi also works.
  - Black stream bug: missing `XR_SWAPCHAIN_USAGE_SAMPLED_BIT`. Busy-loop
    freeze when `shouldRender=0`.
  - Scene-name resolution, floater culling, leveling, world scale.
  - Controllers: A = debug overlay, B = dual-eye screenshot.

---

### 10. Foveation: VRS + blur + 72 Hz (1:30)

- **Key message:** fixed foveation is the biggest VR win; blur makes it look
  clean.
- **Visual:** the A debug overlay (green/yellow/red) full-screen; before/after
  blur crops; chart of no-foveation vs 2x2 vs 4x4 vs fixed.
- Talking points:
  - `VK_KHR_fragment_shading_rate`; per-eye R8_UINT rate maps.
  - 1x1 centre, 2x2 ring, 4x4 outer; 16x16 texel granularity; centred on each
    optical axis.
  - Debug overlay quantized to rate texels (A).
  - 3x3 blur was smaller than a 4x4 block -> 5x5 Gaussian + smoothstep mask;
    mask overlaps the class boundary by 0.08.
  - 72 Hz request via `XR_FB_display_refresh_rate`.
  - room at 0.5: 19.1 -> 12.3 ms (-36%) while keeping the centre sharp.

---

### 11. Results (1:15)

- **Key message:** the cumulative effect is large, across desktop and VR.
- **Visual:** two bar charts:
  1. Desktop room FPS: 11 -> 51 -> 126.
  2. VR room ms: 19.1 no foveation -> 12.3 foveation 4 at 0.5 -> 20.1 at 0.75.
  Plus the resolution table and 0.5/0.75/1.0 crop comparison.
- Talking points:
  - Desktop: CPU sort was the wall; GPU sort + tight quads + culling + idle
    skip.
  - VR: foveation is the wall-breaker; the 72 Hz budget is 13.9 ms.
  - Resolution trade: 0.5 = 12.3 ms, 0.55 = 14.2 ms, 0.6 = 15.4 ms,
    0.75 = 20.1 ms, 1.0 = 28.9 ms; 0.5 is the only reprojection-free point,
    0.75 is the default.
  - Per scene: counter 14.8 ms, bonsai 6.9 ms at 0.75.
  - State the caveat: numbers are from the RTX 5070 Laptop and vary with
    clocks/scene.

---

### 12. Lessons, Vulkan, next steps (1:00)

- **Key message:** each milestone added a Vulkan capability; the engineering
  lessons are as important as the FPS.
- **Visual:** "Vulkan features per phase" strip; maybe a final 5-10 s VR clip.
- Talking points:
  - Vulkan: explicit sync, compute/barriers, indirect draw, dynamic rendering,
    VRS, OpenXR interop.
  - Measure per pass; fix stability (flicker) before micro-optimizing.
  - Modularity (`IPresentation`, `Pass`) paid off when moving to VR.
  - Next: locomotion, dynamic resolution, multiview tiled path, fp16/temporal
    AA, in-VR UI.
- Close with thanks; Q&A.

---

## Backup slides (only if asked)

- Radix sort details: warp-match ranking, stable scatter.
- Tiled raster details: binning, shared-memory staging, early-out.
- VRS rate map encoding and the exact rate values.
- OpenXR call sequence and extensions.
- Full per-scene performance table.
- Bug gallery with before/after screenshots.
- Video capture workflow (how the clips were made).

---

## Assets to capture (map to slides)

| Slide | Asset |
|---|---|
| 1 | Dual-eye room PNG (3712x1944) or 10 s Quest clip |
| 2 | 3DGS diagram + room close-up |
| 3 | Architecture diagram (make in draw.io/Excalidraw) |
| 4 | First desktop screenshot + 5-10 s orbit clip |
| 5 | SH DC vs degree-3 side-by-side; sort pipeline diagram |
| 6 | Before/after bar chart; optional zoomed splat |
| 7 | Tiled pipeline diagram; tiled vs instanced clip |
| 8 | Multi-view / OpenXR loop diagram |
| 9 | Streaming diagram + Quest recording |
| 10 | Foveation debug overlay; blur A/B crops |
| 11 | FPS/ms charts; resolution comparison crops |
| 12 | Final clip / Vulkan feature strip |

---

## Delivery notes

- Rehearse with a timer; 1 minute per content slide is the target.
- If running long, cut slide 6 detail and the tiled deep-dive; keep foveation
  and results.
- Keep charts to 2-3 bars each; the audience should read the trend, not the
  table.
- Label every performance chart with resolution, scene and "RTX 5070 Laptop"
  so the numbers are credible.
- Use the A/B comparisons (SH, blur, tiled) as visual anchors; they are more
  memorable than raw numbers.
