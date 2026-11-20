cmake_minimum_required(VERSION 3.14)
project(someip VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# GitHub mirror prefix for China mainland acceleration (CI sets to empty).
set(OSP_GITHUB_MIRROR "" CACHE STRING "GitHub download mirror prefix (e.g. https://ghfast.top/)")

# newosp is a header-only framework dependency; override for out-of-tree builds.
set(OSP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../newosp/include"
    CACHE PATH "newosp include directory")

option(SOMEIP_BUILD_TESTS "Build tests" ON)
option(SOMEIP_NO_EXCEPTIONS "Build tests with -fno-exceptions -fno-rtti" OFF)

# --- Core: header-only interface library ---
add_library(osp_someip INTERFACE)
target_include_directories(osp_someip INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${OSP_INCLUDE_DIR})
target_compile_features(osp_someip INTERFACE cxx_std_17)
target_compile_options(osp_someip INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(SOMEIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
