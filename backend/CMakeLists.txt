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

# UDP socket library

add_library(iptv_udp STATIC
    src/UdpSocket.cpp
)

target_include_directories(iptv_udp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(iptv_udp PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# HLS segmenter library

add_library(iptv_hls STATIC
    src/HlsSegmenter.cpp
)

target_include_directories(iptv_hls PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(iptv_hls PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Backend application

add_executable(iptv_backend
    src/main.cpp
)

target_link_libraries(iptv_backend PRIVATE
    iptv_udp
    iptv_rtp
    iptv_ts
    iptv_psi
    iptv_hls
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

# UDP socket tests

add_executable(udp_socket_test
    tests/UdpSocketTest.cpp
)

target_link_libraries(udp_socket_test PRIVATE
    iptv_udp
)

target_compile_options(udp_socket_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME udp_socket_unit_test
    COMMAND udp_socket_test
)

set_tests_properties(
    udp_socket_unit_test
    PROPERTIES TIMEOUT 5
)

# HLS segmenter integration test

add_executable(hls_segmenter_test
    tests/HlsSegmenterTest.cpp
)

target_link_libraries(hls_segmenter_test PRIVATE
    iptv_hls
)

target_compile_options(hls_segmenter_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
# Circular disk buffer library

find_package(Threads REQUIRED)

add_library(iptv_buffer STATIC
    src/CircularDiskBuffer.cpp
)

target_include_directories(iptv_buffer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_buffer PUBLIC
    Threads::Threads
)

target_compile_options(iptv_buffer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Circular disk buffer tests

add_executable(circular_disk_buffer_test
    tests/CircularDiskBufferTest.cpp
)

target_link_libraries(circular_disk_buffer_test PRIVATE
    iptv_buffer
)

target_compile_options(circular_disk_buffer_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME circular_disk_buffer_unit_test
    COMMAND circular_disk_buffer_test
)

set_tests_properties(
    circular_disk_buffer_unit_test
    PROPERTIES TIMEOUT 30
)
# HLS playlist parser library

add_library(iptv_hls_playlist STATIC
    src/HlsPlaylistParser.cpp
)

target_include_directories(iptv_hls_playlist PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(iptv_hls_playlist PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# HLS playlist parser tests

add_executable(hls_playlist_parser_test
    tests/HlsPlaylistParserTest.cpp
)

target_link_libraries(hls_playlist_parser_test PRIVATE
    iptv_hls_playlist
)

target_compile_options(hls_playlist_parser_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME hls_playlist_parser_unit_test
    COMMAND hls_playlist_parser_test
)

set_tests_properties(
    hls_playlist_parser_unit_test
    PROPERTIES TIMEOUT 5
)
# HLS buffer manager library

add_library(iptv_hls_buffer STATIC
    src/HlsBufferManager.cpp
)

target_include_directories(iptv_hls_buffer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_hls_buffer PUBLIC
    iptv_buffer
    iptv_hls_playlist
)

target_compile_options(iptv_hls_buffer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# HLS buffer manager tests

add_executable(hls_buffer_manager_test
    tests/HlsBufferManagerTest.cpp
)

target_link_libraries(hls_buffer_manager_test PRIVATE
    iptv_hls_buffer
)

target_compile_options(hls_buffer_manager_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME hls_buffer_manager_unit_test
    COMMAND hls_buffer_manager_test
)

set_tests_properties(
    hls_buffer_manager_unit_test
    PROPERTIES TIMEOUT 10
)
# HLS playlist writer library

add_library(iptv_hls_writer STATIC
    src/HlsPlaylistWriter.cpp
)

target_include_directories(iptv_hls_writer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_hls_writer PUBLIC
    iptv_buffer
)

target_compile_options(iptv_hls_writer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# HLS playlist writer tests

add_executable(hls_playlist_writer_test
    tests/HlsPlaylistWriterTest.cpp
)

target_link_libraries(hls_playlist_writer_test PRIVATE
    iptv_hls_writer
    iptv_hls_playlist
)

target_compile_options(hls_playlist_writer_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME hls_playlist_writer_unit_test
    COMMAND hls_playlist_writer_test
)

set_tests_properties(
    hls_playlist_writer_unit_test
    PROPERTIES TIMEOUT 10
)
# Deferred segment deletion tests

add_executable(deferred_deletion_test
    tests/DeferredDeletionTest.cpp
)

target_link_libraries(deferred_deletion_test PRIVATE
    iptv_buffer
    iptv_hls_writer
)

target_compile_options(deferred_deletion_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME deferred_deletion_unit_test
    COMMAND deferred_deletion_test
)

set_tests_properties(
    deferred_deletion_unit_test
    PROPERTIES TIMEOUT 10
)

target_link_libraries(iptv_backend PRIVATE iptv_hls_buffer iptv_hls_writer)

# Bounded TS queue library

find_package(Threads REQUIRED)

add_library(iptv_ts_queue STATIC
    src/BoundedTsQueue.cpp
)

target_include_directories(iptv_ts_queue PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_ts_queue PUBLIC
    Threads::Threads
)

target_compile_options(iptv_ts_queue PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
# Bounded TS queue tests

add_executable(bounded_ts_queue_test
    tests/BoundedTsQueueTest.cpp
)

target_link_libraries(bounded_ts_queue_test PRIVATE
    iptv_ts_queue
)

target_compile_options(bounded_ts_queue_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_test(
    NAME bounded_ts_queue_unit_test
    COMMAND bounded_ts_queue_test
)

set_tests_properties(
    bounded_ts_queue_unit_test
    PROPERTIES TIMEOUT 10
)
# HLS consumer library

add_library(iptv_hls_consumer STATIC
    src/HlsConsumer.cpp
)

target_include_directories(iptv_hls_consumer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_hls_consumer PUBLIC
    iptv_ts_queue
    iptv_hls
    iptv_hls_buffer
    iptv_hls_writer
    Threads::Threads
)

target_compile_options(iptv_hls_consumer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
# RTP producer library

add_library(iptv_rtp_producer STATIC
    src/RtpProducer.cpp
)

target_include_directories(iptv_rtp_producer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(iptv_rtp_producer PUBLIC
    iptv_udp
    iptv_rtp
    iptv_ts
    iptv_psi
    iptv_ts_queue
    Threads::Threads
)

target_compile_options(iptv_rtp_producer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
# Producer-consumer backend integration

target_link_libraries(iptv_backend PRIVATE
    iptv_rtp_producer
    iptv_hls_consumer
    iptv_ts_queue
    Threads::Threads
)
