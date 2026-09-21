# fwshot: render a .fwscene to a PNG from the command line.
# Uses the CPU reference renderer, so it builds and runs in every configuration
# (including FORGEWORKS_HEADLESS=ON and non-Windows hosts).
add_executable(fwshot ScreenshotTool.cpp)
target_link_libraries(fwshot PRIVATE fw_engine fw_stb)
target_include_directories(fwshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include)

# fwui: render the Map Maker's real user interface (EditorApp) to a PNG using
# the CPU canvas - no GPU, no window, no display required. Also reports how much
# of the frame the interface covers, so an empty UI fails loudly instead of
# silently shipping as a blank editor.
add_executable(fwui EditorUiShot.cpp)
target_link_libraries(fwui PRIVATE fw_editor fw_engine fw_stb)
target_include_directories(fwui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../editor/include
)
