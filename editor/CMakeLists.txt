# The editor's UI lives in a library so it can be linked by both the Windows
# executable and fwui (tools/EditorUiShot.cpp), which renders the very same
# interface to a PNG on the CPU - no GPU, no window, any platform.
add_library(fw_editor STATIC
    src/EditorApp.cpp
    src/panels/InspectorPanel.cpp
    src/panels/AssetBrowserPanel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ScriptEditorPanel.cpp
    src/panels/HammerModePanel.cpp
    src/panels/LightmapBakePanel.cpp
)
target_link_libraries(fw_editor PUBLIC fw_engine fw_stb)
target_include_directories(fw_editor PUBLIC include)

# The Map Maker itself is a Windows GUI application.
if(NOT FORGEWORKS_HEADLESS AND WIN32 AND FORGEWORKS_BUILD_EDITOR)
    add_executable(ForgeworksEditor WIN32 src/main.cpp)
    target_link_libraries(ForgeworksEditor PRIVATE fw_editor fw_engine fw_stb)
    target_include_directories(ForgeworksEditor PRIVATE include)
endif()
