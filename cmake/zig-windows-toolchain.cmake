# CMake toolchain file that cross-compiles the Windows build (D3D11/Win32)
# using Zig's bundled clang + mingw-w64 headers/import libs. This is used
# ONLY for validating that the Windows-specific engine/editor/game code
# actually compiles and links correctly from this Linux sandbox; the
# officially supported way to build Forgeworks is MSVC + Visual Studio on
# real Windows (see README.md).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED ZIG_EXECUTABLE)
    set(ZIG_EXECUTABLE "/tmp/venv/bin/python3 -m ziglang")
endif()

set(ZIG_TARGET "x86_64-windows-gnu")

set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc.sh")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cxx.sh")
set(CMAKE_RC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-rc.sh")

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_EXECUTABLE_SUFFIX ".exe")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")

# Flag consumed by third_party/CMakeLists.txt: zig's bundled mingw-w64 only
# ships versioned d3dcompiler import libs (d3dcompiler_47 etc.), unlike the
# real Windows SDK/MSVC which provides an unversioned d3dcompiler.lib.
set(FORGEWORKS_USING_MINGW_IMPORT_LIBS TRUE)
