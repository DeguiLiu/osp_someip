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

add_executable(someip_tests
    test_message_id.cpp
    test_message_type.cpp
    test_return_code.cpp
    test_header.cpp
    test_message.cpp
    test_wire_golden.cpp
    test_sd_entry.cpp
    test_sd_options.cpp
    test_sd_header.cpp
)
target_link_libraries(someip_tests Catch2::Catch2WithMain osp_someip)

if(SOMEIP_NO_EXCEPTIONS)
    target_compile_options(someip_tests PRIVATE -fno-exceptions -fno-rtti)
endif()

catch_discover_tests(someip_tests)
