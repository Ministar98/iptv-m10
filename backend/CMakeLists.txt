add_executable(iptv_backend
    src/main.cpp
    src/UdpSocket.cpp
)

target_include_directories(iptv_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
