# Rendering: conventions, passes and how to debug a blank viewport

This document records the conventions the whole rendering stack shares, and the
tooling that makes "the viewport shows nothing" diagnosable without a GPU.

## Conventions (do not break these)

| Thing | Convention | Where |
|---|---|---|
| View space | Right handed, camera looks down **-Z** | `engine/include/engine/render/RenderTypes.h` |
| Projection | `glm::perspectiveRH_ZO` - clip depth **0..1** (Direct3D) | `MakeProjectionMatrix()` |
| Matrix upload | glm matrices (column-major) go into constant buffers verbatim | `engine/src/render/Renderer.cpp` |
| HLSL multiplication | **always `mul(matrix, vector)`** | `assets/shaders/Common.hlsli` |
| Triangle winding | counter-clockwise as seen from outside; D3D11 default rasterizer culls back faces | `engine/src/asset/MeshData.cpp` |
| Yaw/pitch | `yaw = 0` looks down -Z, positive yaw turns towards -X, positive pitch looks up - identical to `Transform::SetEulerDegrees(vec3(pitch, yaw, 0))` | `YawPitchForward()` |
| Output | linear light -> Reinhard tonemap -> gamma 2.2 (`FWToDisplay()`) | `Common.hlsli`, mirrored in `SoftwareRenderer.cpp` |

The most expensive historical bug in this project came from mixing these up: uploading
glm matrices (column-major) and then writing `mul(vector, matrix)` in HLSL multiplies by
the *transpose*. For the perspective matrix that moves every vertex outside the clip
volume, which renders as a perfectly blank viewport with no error anywhere. Keeping the
CPU reference renderer and the GPU shaders on the same conventions - and testing them -
is what prevents a silent repeat.

## Passes (D3D11, `engine/src/render/Renderer.cpp`)

1. **Shadow pass** - depth-only render from the primary shadow-casting directional
   light into a 2048x2048 D32 map. The light frustum is fitted around the camera's view
   (it follows the camera, so shadows don't disappear when you fly away from the origin).
2. **Skybox** - fullscreen triangle, procedural gradient, depth tested, depth writes off.
3. **Opaque** - forward lit meshes: PBR-ish term per light, optional baked lightmap,
   optional PCF shadow lookup for light 0.
4. **Grid** - fullscreen triangle that raycasts each pixel onto the `y = 0` plane and
   draws the editor grid (`assets/shaders/Grid.hlsl`). Depth tested, depth writes off,
   faded at grazing angles and with distance. This is why the viewport is readable even
   in a scene that contains no geometry at all.
5. **ImGui** - the editor's UI, drawn by the ImGui DX11 backend on top.

The editor renders passes 1-4 into an off-screen texture and shows it with
`ImGui::Image()` (Godot's "viewport as texture" approach).

## CPU reference renderer

`engine/render/SoftwareRenderer.h` rasterises the same scene on the CPU: same camera
math, same lighting formula, same tonemap, plus the sky and grid. It is deliberately
simple (no shadows, no post, no texture sampling, single threaded) and exists to:

* keep the viewport visible on machines/CI without a GPU (`View > CPU Viewport Preview`),
* let the editor fall back automatically when the D3D11 path is unavailable (missing
  shader, failed render target), instead of showing an empty panel,
* produce screenshots and the "viewport is not blank" regression test
  (`engine_tests`, writes `test_output/viewport.png`).

## Tools

```bash
# any .fwscene -> PNG, no GPU required
./build-headless/bin/fwshot assets/scenes/default.fwscene out.png [--camera top|front|side|persp|scene]
                                                                 [--pos x,y,z --look x,y,z]
                                                                 [--wireframe] [--no-grid]

# whole editor window -> PNG (Windows)
build\bin\ForgeworksEditor.exe --screenshot shots\editor.png --frames 30

# game runtime
build\bin\ForgeworksGame.exe --play --screenshot shots\game.png
```

All three write through `engine/core/Paths.h`, which locates the project root
(from `--root`, `FORGEWORKS_ROOT`, the executable's location, or the working
directory), so tools work no matter where they are launched from.

## Checklist for a blank viewport

1. Console panel: shader compile errors, missing scene, failed render target creation are
   all logged explicitly (with the resolved path).
2. Viewport overlay: says whether the GPU or CPU renderer is active, whether the grid is
   on, and where the camera is.
3. `View > Reset Camera` / `F` to frame the selection - the camera may simply be pointing
   at empty space.
4. `View > CPU Viewport Preview` - if this shows geometry, the D3D11 path is at fault; if
   it is blank too, the scene/camera is.
5. `fwshot scene.fwscene check.png` prints entity/triangle/coverage statistics - a quick
   way to tell "nothing to draw" from "nothing drawn".
