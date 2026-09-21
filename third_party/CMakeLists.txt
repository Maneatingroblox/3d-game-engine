# ---------------------------------------------------------------------------
# Vendored third-party dependencies. Each is wrapped in a small INTERFACE or
# STATIC target so the rest of the project can just target_link_libraries()
# against a clean name.
# ---------------------------------------------------------------------------

# ---- nlohmann/json (header only) ----
add_library(fw_json INTERFACE)
target_include_directories(fw_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json)

# ---- GLM (header only) ----
add_library(fw_glm INTERFACE)
target_include_directories(fw_glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glm)
target_compile_definitions(fw_glm INTERFACE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_SILENT_WARNINGS)

# ---- EnTT (header only ECS) ----
add_library(fw_entt INTERFACE)
target_include_directories(fw_entt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/include)

# ---- stb (header only) ----
add_library(fw_stb INTERFACE)
target_include_directories(fw_stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# ---- tinyobjloader (header only) ----
add_library(fw_tinyobj INTERFACE)
target_include_directories(fw_tinyobj INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader)

# ---- Lua 5.5 (built as a C static lib) ----
# Exclude the standalone-interpreter/compiler/amalgamation sources: lua.c and
# luac.c each define main(), and onelua.c #includes every other .c file
# (Lua's single-TU amalgamated build option) - compiling any of them
# alongside the rest of lua/src/*.c produces duplicate symbols / "file not
# found" errors, so the library is built from the individual sources only.
file(GLOB FW_LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/*.c)
list(REMOVE_ITEM FW_LUA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/lua.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/luac.c
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/src/onelua.c
)
add_library(fw_lua STATIC ${FW_LUA_SOURCES})
target_include_directories(fw_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lua/src)
if(WIN32)
    # On Windows, luaconf.h auto-defines LUA_USE_WINDOWS when _WIN32 is present.
    # Its guard is #if !defined(LUA_USE_C89) && defined(_WIN32) ... so if we
    # also define it via /D on the command line, the header re-defines it and
    # MSVC emits C4005 macro redefinition for every Lua source file.
    # Therefore we intentionally do NOT define LUA_USE_WINDOWS here and let
    # luaconf.h handle it. This eliminates all C4005 warnings.
else()
    target_compile_definitions(fw_lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(fw_lua PUBLIC m dl)
endif()
set_target_properties(fw_lua PROPERTIES LINKER_LANGUAGE C)
# Extra safety: if for any reason LUA_USE_WINDOWS still ends up defined via
# other compile definitions, suppress the harmless C4005 warning for this
# third-party target only.
if(MSVC)
    target_compile_options(fw_lua PRIVATE /wd4005)
endif()

# ---- sol2 (header only, sits on top of fw_lua) ----
add_library(fw_sol2 INTERFACE)
target_include_directories(fw_sol2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sol2/include)
target_compile_definitions(fw_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
target_link_libraries(fw_sol2 INTERFACE fw_lua)

# ---- Jolt Physics (real subdirectory build) ----
# JPH_USE_DX12/VK/METAL/CPU_COMPUTE only gate Jolt's optional GPU broadphase
# debug-visualization backends, not the physics simulation itself - Forgeworks
# doesn't use them (rendering is entirely our own D3D11 Renderer), and
# leaving DX12 on pulls in d3dcompiler.lib/d3d12.lib which the mingw-w64
# toolchain (used for this repo's Zig cross-compile validation build) only
# ships as versioned import libs, not the unversioned names Jolt hardcodes.
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
# Critical: Jolt defaults to static MSVC runtime (MT/MTd) via
# USE_STATIC_MSVC_RUNTIME_LIBRARY=ON. Forgeworks (and the MSVC default)
# uses dynamic runtime (MD/MDd). Mismatch causes:
#   LNK2038: mismatch detected for 'RuntimeLibrary': MT_StaticRelease vs MD_DynamicRelease
#   LNK4098: defaultlib 'LIBCMT' conflicts
#   LNK2005: multiply defined symbols from msvcprt vs libcpmt
# Forcing this OFF makes Jolt use MD/MDd, matching the rest of the project.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
# Also ensure Jolt's CMAKE_MSVC_RUNTIME_LIBRARY is set to MD/MDd even if
# the parent scope already forced it - Jolt's own CMakeLists checks
# USE_STATIC_MSVC_RUNTIME_LIBRARY and would otherwise override.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/jolt/Build EXCLUDE_FROM_ALL)
add_library(fw_jolt INTERFACE)
target_link_libraries(fw_jolt INTERFACE Jolt)
target_include_directories(fw_jolt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/jolt)

# ---- Dear ImGui + ImGuizmo (platform independent core, built everywhere) ----
# The core library and ImGuizmo contain no platform code, so tools that render
# the editor UI on the CPU (fwui) and the headless validation build use the same
# Dear ImGui the editor uses.
add_library(fw_imgui_core STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imguizmo/ImGuizmo.cpp
)
target_include_directories(fw_imgui_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo
)

# ---- Win32 + DX11 backends (Windows / editor build only) ----
if(NOT FORGEWORKS_HEADLESS)
    add_library(fw_imgui STATIC
        imgui/backends/imgui_impl_win32.cpp
        imgui/backends/imgui_impl_dx11.cpp
    )
    target_link_libraries(fw_imgui PUBLIC fw_imgui_core)
    if(WIN32)
        # The Windows SDK (MSVC) ships an unversioned d3dcompiler.lib, but
        # mingw-w64 (including clang -target *-windows-gnu, e.g. building
        # via Zig) only provides versioned import libs (d3dcompiler_47 etc.)
        # - link the right name for whichever toolchain is active.
        if(MINGW OR FORGEWORKS_USING_MINGW_IMPORT_LIBS)
            target_link_libraries(fw_imgui PUBLIC d3d11 dxgi d3dcompiler_47 dwmapi)
        else()
            target_link_libraries(fw_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)
        endif()
    endif()
endif()

# ---- miniaudio (header only, Windows / non-headless build only) ----
if(NOT FORGEWORKS_HEADLESS)
    add_library(fw_miniaudio INTERFACE)
    target_include_directories(fw_miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)
endif()
