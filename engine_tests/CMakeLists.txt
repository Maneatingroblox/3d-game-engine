add_executable(engine_tests main.cpp)
# fw_stb provides the stb headers used by the viewport test's PNG output.
target_link_libraries(engine_tests PRIVATE fw_engine fw_stb)
