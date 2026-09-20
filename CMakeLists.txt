cmake_minimum_required(VERSION 3.20)
project(Forgeworks VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(FORGEWORKS_HEADLESS "Build only platform-independent engine core + tests (no D3D11/Win32/audio). Used for cross-platform validation." OFF)
option(FORGEWORKS_BUILD_EDITOR "Build the Map Maker editor (Windows only)" ON)
option(FORGEWORKS_BUILD_GAME "Build the standalone game runtime (Windows only)" ON)

if(NOT FORGEWORKS_HEADLESS AND NOT WIN32)
    message(WARNING "Forgeworks targets Windows + DirectX 11. Building on a non-Windows host without "
                     "FORGEWORKS_HEADLESS=ON will fail once the renderer/platform sources are compiled. "
                     "Pass -DFORGEWORKS_HEADLESS=ON to build/validate only the platform-independent core.")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
    # The engine calls the explicit *W (wide-char) Win32 APIs everywhere
    # (LoadCursorW, RegisterClassExW, ...), so force the UNICODE/_UNICODE
    # macros on for every compiler - without them, TCHAR-based macros like
    # IDC_ARROW resolve to the ANSI (LPSTR) overloads and every *W call site
    # fails to compile against LPCWSTR parameters.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()
if(MSVC)
    add_compile_options(/MP /W3 /permissive-)
endif()

add_subdirectory(third_party)
add_subdirectory(engine)

if(FORGEWORKS_HEADLESS)
    add_subdirectory(engine_tests)
else()
    if(FORGEWORKS_BUILD_EDITOR)
        add_subdirectory(editor)
    endif()
    if(FORGEWORKS_BUILD_GAME)
        add_subdirectory(game)
    endif()
endif()
