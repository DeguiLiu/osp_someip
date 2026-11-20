include(FetchContent)

set(CATCH2_VERSION v3.5.2)

# DOWNLOAD_EXTRACT_TIMESTAMP needs CMake 3.24; older CMake rejects it.
set(_someip_catch2_ts "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(_someip_catch2_ts DOWNLOAD_EXTRACT_TIMESTAMP ON)
endif()

FetchContent_Declare(
    Catch2
    URL ${OSP_GITHUB_MIRROR}https://github.com/catchorg/Catch2/archive/refs/tags/${CATCH2_VERSION}.zip
    ${_someip_catch2_ts}
)
FetchContent_MakeAvailable(Catch2)
include(Catch)

find_package(Threads REQUIRED)

# Compile-only guard: every header, including the server template, must build
# under the interface flags (notably -fno-exceptions -fno-rtti when enabled).
add_library(someip_header_smoke OBJECT header_smoke.cpp)
target_link_libraries(someip_header_smoke PRIVATE osp_someip)

add_executable(someip_no_alloc no_alloc_witness.cpp)
target_link_libraries(someip_no_alloc PRIVATE osp_someip)
add_test(NAME someip_no_alloc COMMAND someip_no_alloc)

# Every public header must compile on its own. Skipped when cross compiling:
# the check runs the compiler directly, outside CMake's toolchain environment.
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME someip_headers_self_contained
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_headers.sh
                ${CMAKE_CXX_COMPILER} ${CMAKE_SOURCE_DIR}/include ${OSP_INCLUDE_DIR})
    add_test(NAME someip_rtthread_core_headers
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_rtthread.sh
                ${CMAKE_CXX_COMPILER} ${OSP_INCLUDE_DIR})
    # Needs an lwIP checkout, which is not vendored: pass -DSOMEIP_LWIP_DIR=<dir>.
    if(SOMEIP_LWIP_DIR)
        add_test(NAME someip_lwip_socket_backend
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_lwip.sh
                    ${CMAKE_CXX_COMPILER} ${SOMEIP_LWIP_DIR} ${OSP_INCLUDE_DIR})
    endif()
endif()

add_executable(someip_tests
    test_message_id.cpp
    test_message_type.cpp
    test_return_code.cpp
    test_header.cpp
    test_message.cpp
    test_tp.cpp
    test_tp_fuzz.cpp
    test_wire_golden.cpp
    test_sd_entry.cpp
    test_sd_options.cpp
    test_sd_header.cpp
    test_sd_parse.cpp
    test_server.cpp
    test_client.cpp
    test_udp_loopback.cpp
    test_multicast.cpp
    test_client_server_loopback.cpp
    test_e2e.cpp
    test_e2e_profile1.cpp
    test_e2e_profile4.cpp
    test_e2e_profile7.cpp
    test_e2e_fuzz.cpp
    test_e2e_profile5.cpp
    test_sd_fuzz.cpp
    test_owned_payload.cpp
)
target_link_libraries(someip_tests Catch2::Catch2WithMain osp_someip Threads::Threads)

if(SOMEIP_NO_EXCEPTIONS)
    target_compile_options(someip_tests PRIVATE -fno-exceptions -fno-rtti)
endif()

# Catch2 exits with 4 when a case is skipped; CTest must not read that as a
# failure. SOMEIP_TEST_MCAST_IFACE is forwarded to the cases that look for it,
# so host runs can exercise multicast without exporting it in the shell.
set(_someip_test_properties PROPERTIES SKIP_RETURN_CODE 4)
if(SOMEIP_TEST_MCAST_IFACE)
    list(APPEND _someip_test_properties
        ENVIRONMENT "SOMEIP_TEST_MCAST_IFACE=${SOMEIP_TEST_MCAST_IFACE}")
endif()
catch_discover_tests(someip_tests ${_someip_test_properties})
