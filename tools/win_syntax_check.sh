#!/usr/bin/env bash
# Cross-compile syntax check for the Windows-only sources.
#
# WHY THIS EXISTS
# ---------------
# The headless CMake target deliberately excludes every file that touches Win32
# or D3D11 (Application.cpp, Renderer.cpp, Window.cpp, RenderDevice.cpp, the
# editor panels, ...). That means a green headless build proves NOTHING about
# whether those files still compile with MSVC - a change can look perfectly
# fine here and then break the real Windows build.
#
# This script closes that gap using zig's bundled clang + mingw-w64 headers,
# which provide windows.h, d3d11.h and friends. It only runs -fsyntax-only:
# the goal is to catch type errors and platform mistakes (the classic one being
# fs::path::native(), which is std::wstring on Windows but std::string here),
# not to produce a shippable binary.
#
# Usage:  tools/win_syntax_check.sh [extra source files...]
set -uo pipefail
cd "$(dirname "$0")/.."

export PATH="$HOME/.local/bin:$PATH"
# NOTE: use real -c compilation, not -fsyntax-only: zig's driver rejects
# -fsyntax-only with a bare "FileNotFound" that looks like a missing source.
ZIG=(python3 -m ziglang c++ -target x86_64-windows-gnu -std=c++20 -c)

DEFS=(-DGLM_ENABLE_EXPERIMENTAL -DUNICODE -D_UNICODE -DNOMINMAX
      -DWIN32_LEAN_AND_MEAN -DFW_BUILD_ID='"syntaxcheck"')

INC=(-I engine/include -I editor/include -I game/include
     -I third_party/glm -I third_party/stb -I third_party/tinyobjloader
     -I third_party/json -I third_party/entt/include -I third_party/sol2/include
     -I third_party/imgui -I third_party/imgui/misc/cpp -I third_party/imguizmo
     -I third_party/lua/src -I third_party/jolt -I third_party/miniaudio)

# Every engine/editor/game translation unit. The Windows-only ones (Application,
# Window, Renderer, RenderDevice, the editor panels, ...) are the point of this
# script, but checking all of them is cheap and catches portability mistakes in
# the shared code too.
mapfile -t SOURCES < <(ls engine/src/*/*.cpp editor/src/*.cpp editor/src/panels/*.cpp \
                          game/src/*.cpp 2>/dev/null)
SOURCES+=("$@")

TMPDIR_OBJ=$(mktemp -d)
trap 'rm -rf "$TMPDIR_OBJ"' EXIT

fails=0
for src in "${SOURCES[@]}"; do
  [ -f "$src" ] || { printf '  SKIP  %s (not found)\n' "$src"; continue; }
  if out=$("${ZIG[@]}" "${DEFS[@]}" "${INC[@]}" "$src" -o "$TMPDIR_OBJ/$(echo "$src" | tr / _).obj" 2>&1); then
    printf '  ok    %s\n' "$src"
  else
    printf '  FAIL  %s\n' "$src"
    printf '%s\n' "$out" | grep -E 'error:' | head -12 | sed 's/^/          /'
    fails=$((fails + 1))
  fi
done

echo
if [ "$fails" -eq 0 ]; then
  echo "All Windows-only sources pass the syntax check."
else
  echo "$fails file(s) FAILED the Windows syntax check."
fi
exit "$fails"
