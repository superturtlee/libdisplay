cmake_minimum_required(VERSION 3.10)
project(virtual_drm C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")

add_library(socket_utils STATIC common/socket_utils.c)

if(ANDROID)
    add_library(display_consumer SHARED libdisplay_consumer/display_consumer.c)
    target_link_libraries(display_consumer socket_utils)
else()
    add_executable(display_daemon daemon/daemon.c)
    target_link_libraries(display_daemon socket_utils pthread)

    add_library(display_producer SHARED libdisplay_producer/display_producer.c)
    target_link_libraries(display_producer socket_utils pthread)

    add_library(display_consumer SHARED libdisplay_consumer/display_consumer.c)
    target_link_libraries(display_consumer socket_utils pthread)

    add_executable(test_consumer tests/test_consumer.c)
    target_link_libraries(test_consumer display_consumer socket_utils pthread)

    add_executable(test_producer tests/test_producer.c)
    target_link_libraries(test_producer display_producer socket_utils pthread)

    add_executable(bench_consumer tests/bench_consumer.c)
    target_link_libraries(bench_consumer display_consumer socket_utils pthread)

    add_executable(bench_producer tests/bench_producer.c)
    target_link_libraries(bench_producer display_producer socket_utils pthread)

    add_executable(test_triple_consumer tests/test_triple_consumer.c)
    target_link_libraries(test_triple_consumer display_consumer socket_utils pthread)

    add_executable(test_triple_producer tests/test_triple_producer.c)
    target_link_libraries(test_triple_producer display_producer socket_utils pthread)

    add_executable(bench_input_consumer tests/bench_input_consumer.c)
    target_link_libraries(bench_input_consumer display_consumer socket_utils pthread)

    add_executable(bench_input_producer tests/bench_input_producer.c)
    target_link_libraries(bench_input_producer display_producer socket_utils pthread)
endif()
