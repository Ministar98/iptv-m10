add_library(iptv_rtp STATIC
    src/RtpParser.cpp
)

target_include_directories(iptv_rtp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(iptv_rtp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(iptv_backend
    src/main.cpp
    src/UdpSocket.cpp
)

target_link_libraries(iptv_backend PRIVATE
    iptv_rtp
)

target_compile_options(iptv_backend PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(rtp_parser_test
    tests/RtpParserTest.cpp
)

target_link_libraries(rtp_parser_test PRIVATE
    iptv_rtp
)

target_compile_options(rtp_parser_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME rtp_parser_unit_test
    COMMAND rtp_parser_test
)

set_tests_properties(
    rtp_parser_unit_test
    PROPERTIES TIMEOUT 5
)
