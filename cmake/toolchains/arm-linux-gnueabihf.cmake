# Cross-compilation toolchain for 32-bit ARM Linux.
#
#   cmake -S . -B build-arm \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-linux-gnueabihf.cmake
#
# Replace the compiler names with the ones your SDK ships.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
