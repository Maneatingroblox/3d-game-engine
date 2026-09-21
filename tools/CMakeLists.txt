# fwshot: render a .fwscene to a PNG from the command line.
# Uses the CPU reference renderer, so it builds and runs in every configuration
# (including FORGEWORKS_HEADLESS=ON and non-Windows hosts).
add_executable(fwshot ScreenshotTool.cpp)
target_link_libraries(fwshot PRIVATE fw_engine fw_stb)
target_include_directories(fwshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include)
