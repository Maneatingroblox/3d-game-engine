add_executable(ForgeworksEditor WIN32
    src/main.cpp
    src/EditorApp.cpp
    src/panels/InspectorPanel.cpp
    src/panels/AssetBrowserPanel.cpp
    src/panels/ConsolePanel.cpp
    src/panels/ScriptEditorPanel.cpp
    src/panels/HammerModePanel.cpp
    src/panels/LightmapBakePanel.cpp
)
target_link_libraries(ForgeworksEditor PRIVATE fw_engine)
# The editor saves viewport screenshots (stb_image_write), so it needs the
# vendored stb headers on its include path (fw_stb is an INTERFACE target).
target_link_libraries(ForgeworksEditor PRIVATE fw_stb)
target_include_directories(ForgeworksEditor PRIVATE include)
