cmake_minimum_required(VERSION 3.14)
project(someip VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# GitHub mirror prefix for China mainland acceleration (CI sets to empty).
set(OSP_GITHUB_MIRROR "" CACHE STRING "GitHub download mirror prefix (e.g. https://ghfast.top/)")

# newosp is a header-only framework dependency; override for out-of-tree builds.
set(OSP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../newosp/include"
    CACHE PATH "newosp include directory")

option(SOMEIP_FETCH_NEWOSP "Fetch newosp from GitHub when OSP_INCLUDE_DIR is missing" OFF)

if(SOMEIP_FETCH_NEWOSP AND NOT EXISTS "${OSP_INCLUDE_DIR}")
    set(OSP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OSP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(_someip_fetch_ts "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        set(_someip_fetch_ts DOWNLOAD_EXTRACT_TIMESTAMP ON)
    endif()
    FetchContent_Declare(
        newosp
        URL ${OSP_GITHUB_MIRROR}https://github.com/DeguiLiu/newosp/archive/refs/heads/main.zip
        ${_someip_fetch_ts}
    )
    FetchContent_MakeAvailable(newosp)
    set(OSP_INCLUDE_DIR "${newosp_SOURCE_DIR}/include")
endif()

option(SOMEIP_BUILD_TESTS "Build tests" ON)
option(SOMEIP_BUILD_EXAMPLES "Build examples" OFF)
option(SOMEIP_NO_EXCEPTIONS "Build tests with -fno-exceptions -fno-rtti" OFF)

# --- Core: header-only interface library ---
add_library(osp_someip INTERFACE)
target_include_directories(osp_someip INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${OSP_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_compile_features(osp_someip INTERFACE cxx_std_17)
target_compile_options(osp_someip INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(SOMEIP_NO_EXCEPTIONS)
    # Applies to every consumer of the header-only library, not just tests.
    target_compile_options(osp_someip INTERFACE -fno-exceptions -fno-rtti)
endif()

if(SOMEIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SOMEIP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Install / package config ---
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS osp_someip EXPORT osp_someipTargets)
install(DIRECTORY include/osp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT osp_someipTargets
    FILE osp_someipTargets.cmake
    NAMESPACE osp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/osp_someip)

configure_package_config_file(
    cmake/osp_someipConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/osp_someipConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/osp_someip)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/osp_someipConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/osp_someipConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/osp_someipConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/osp_someip)
