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
target_include_directories(ForgeworksEditor PRIVATE include)
