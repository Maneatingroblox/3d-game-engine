add_executable(ForgeworksGame WIN32
    src/main.cpp
    src/GameApp.cpp
)
target_link_libraries(ForgeworksGame PRIVATE fw_engine)
target_include_directories(ForgeworksGame PRIVATE include)
