# Screenshots

These images are rendered by the engine itself, not hand-drawn or mocked up.

| File | How it was produced |
|---|---|
| `viewport_scene_camera.png` | `fwshot assets/scenes/default.fwscene ...` using the scene's `Main Camera` - what the Map Maker's viewport shows on first launch |
| `viewport_persp.png` | same scene, free perspective camera |
| `viewport_top.png` | same scene, top-down camera (checks the level from above) |
| `viewport_wireframe.png` | same scene, `--wireframe` |
| `game_runtime.png` | same scene through the game runtime's camera settings |

Regenerate them with:

```bash
cmake -S . -B build-headless -DFORGEWORKS_HEADLESS=ON
cmake --build build-headless -j
./build-headless/bin/fwshot assets/scenes/default.fwscene docs/screenshots/viewport_scene_camera.png
./build-headless/bin/fwshot assets/scenes/default.fwscene docs/screenshots/viewport_persp.png --camera persp --pos 9,6,12 --look 0,1,0
./build-headless/bin/fwshot assets/scenes/default.fwscene docs/screenshots/viewport_top.png --camera top --pos 0,26,0.01 --look 0,0,0
./build-headless/bin/fwshot assets/scenes/default.fwscene docs/screenshots/viewport_wireframe.png --pos 5,3.4,7 --look 0,0.8,-0.4 --wireframe
./build-headless/bin/fwshot assets/scenes/default.fwscene docs/screenshots/game_runtime.png --pos 5,2.9,6.4 --look 0.2,0.8,-0.5 --fov 65
```

`fwshot` uses the CPU reference renderer (`engine/render/SoftwareRenderer.h`), so the
pictures show geometry, lighting, sky and grid exactly as the real camera math produces
them - without needing a GPU, and reproducibly from a headless build.

For an image of the actual editor UI, run the Windows build with

```powershell
build\bin\ForgeworksEditor.exe --screenshot shots\editor.png --frames 30
```
