# The game runtime shell (menu/HUD/pause) lives in a library so it can be linked
# by both the Windows executable and fwui (tools/EditorUiShot.cpp --game), which
# renders the same UI and the same scene through the CPU canvas - that is how the
# game screenshots in docs/screenshots are produced without a GPU.
add_library(fw_game STATIC src/GameApp.cpp)
target_link_libraries(fw_game PUBLIC fw_engine)
target_include_directories(fw_game PUBLIC include)

# The player-facing executable.
if(NOT FORGEWORKS_HEADLESS AND WIN32 AND FORGEWORKS_BUILD_GAME)
    add_executable(ForgeworksGame WIN32 src/main.cpp)
    target_link_libraries(ForgeworksGame PRIVATE fw_game fw_engine)
    target_include_directories(ForgeworksGame PRIVATE include)
endif()
