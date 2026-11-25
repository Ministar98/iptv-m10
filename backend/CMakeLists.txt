# RTP parser library

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

# MPEG-TS parser library

add_library(iptv_ts STATIC
    src/TsParser.cpp
)

target_include_directories(iptv_ts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(iptv_ts PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# PSI library

add_library(iptv_psi STATIC
    src/PsiSectionAssembler.cpp
    src/PatParser.cpp
    src/PmtParser.cpp
)

target_include_directories(iptv_psi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_psi PUBLIC
    iptv_ts
)

target_compile_options(iptv_psi PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Backend application

add_executable(iptv_backend
    src/main.cpp
    src/UdpSocket.cpp
)

target_link_libraries(iptv_backend PRIVATE
    iptv_rtp
    iptv_ts
    iptv_psi
)

target_compile_options(iptv_backend PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# RTP parser tests

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

# MPEG-TS parser tests

add_executable(ts_parser_test
    tests/TsParserTest.cpp
)

target_link_libraries(ts_parser_test PRIVATE
    iptv_ts
)

target_compile_options(ts_parser_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME ts_parser_unit_test
    COMMAND ts_parser_test
)

set_tests_properties(
    ts_parser_unit_test
    PROPERTIES TIMEOUT 5
)

# PSI assembler tests

add_executable(psi_assembler_test
    tests/PsiSectionAssemblerTest.cpp
)

target_link_libraries(psi_assembler_test PRIVATE
    iptv_psi
)

target_compile_options(psi_assembler_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME psi_assembler_unit_test
    COMMAND psi_assembler_test
)

set_tests_properties(
    psi_assembler_unit_test
    PROPERTIES TIMEOUT 5
)

# PAT parser tests

add_executable(pat_parser_test
    tests/PatParserTest.cpp
)

target_link_libraries(pat_parser_test PRIVATE
    iptv_psi
)

target_compile_options(pat_parser_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME pat_parser_unit_test
    COMMAND pat_parser_test
)

set_tests_properties(
    pat_parser_unit_test
    PROPERTIES TIMEOUT 5
)

# PMT parser tests

add_executable(pmt_parser_test
    tests/PmtParserTest.cpp
)

target_link_libraries(pmt_parser_test PRIVATE
    iptv_psi
)

target_compile_options(pmt_parser_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME pmt_parser_unit_test
    COMMAND pmt_parser_test
)

set_tests_properties(
    pmt_parser_unit_test
    PROPERTIES TIMEOUT 5
)
