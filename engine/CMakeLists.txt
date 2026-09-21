# ---------------------------------------------------------------------------
# Forgeworks engine core. When FORGEWORKS_HEADLESS is ON we compile only the
# platform-independent subset (math/ecs/scene/physics/script/asset/brush/
# lightmap) so the simulation logic can be validated on any host. The full
# build additionally compiles engine/src/render + engine/src/platform + audio
# (D3D11/Win32/miniaudio), which require Windows.
# ---------------------------------------------------------------------------

set(FW_CORE_SOURCES
    src/scene/Scene.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsSystem.cpp
    src/script/ScriptEngine.cpp
    src/asset/MeshData.cpp
    src/asset/AssetDatabase.cpp
    src/brush/Brush.cpp
    src/brush/BrushMap.cpp
    src/lightmap/Lightmapper.cpp
    src/scene/SceneSerializer.cpp
    src/scene/DefaultScene.cpp
    src/core/Settings.cpp
    src/core/Engine.cpp
    # Paths: locating the project root so content loads no matter what the
    # working directory is (see engine/core/Paths.h).
    src/core/Paths.cpp
    # Material loading is platform independent (only Bind() is D3D11); the CPU
    # reference renderer needs it too.
    src/render/MaterialSystem.cpp
    # CPU reference renderer: lets the viewport be rendered (and screenshotted)
    # without a GPU, and backs the "viewport is not blank" regression test.
    src/render/SoftwareRenderer.cpp
)

set(FW_CORE_LIBS fw_json fw_glm fw_entt fw_stb fw_tinyobj fw_sol2 fw_jolt)

if(NOT FORGEWORKS_HEADLESS)
    list(APPEND FW_CORE_SOURCES
        src/audio/AudioEngine.cpp
        src/render/RenderDevice.cpp
        src/render/Renderer.cpp
        src/render/ShaderLibrary.cpp
        src/platform/Window.cpp
        src/platform/Input.cpp
        src/asset/TextureLoader.cpp
        src/asset/GpuMeshCache.cpp
        src/core/Application.cpp
    )
    list(APPEND FW_CORE_LIBS fw_imgui fw_miniaudio)
endif()

add_library(fw_engine STATIC ${FW_CORE_SOURCES})
target_include_directories(fw_engine PUBLIC include)
target_link_libraries(fw_engine PUBLIC ${FW_CORE_LIBS})

if(FORGEWORKS_HEADLESS)
    target_compile_definitions(fw_engine PUBLIC FORGEWORKS_HEADLESS=1)
endif()
