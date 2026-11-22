add_executable(iptv_backend
    src/main.cpp
)

target_compile_options(iptv_backend PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME backend_smoke_test
    COMMAND iptv_backend
)
