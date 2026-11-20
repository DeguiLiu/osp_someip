# Cross-compile for 64-bit ARM Linux.
#
# Point AARCH64_TOOLCHAIN_ROOT at a directory whose layout matches a Debian
# root: usr/bin/aarch64-linux-gnu-g++-11 and usr/aarch64-linux-gnu/lib must
# exist. The Ubuntu cross packages can be unpacked there without root.
#
#   export AARCH64_TOOLCHAIN_ROOT=/opt/cross-aarch64/root
#   cmake -S . -B build/aarch64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{AARCH64_TOOLCHAIN_ROOT})
  message(FATAL_ERROR "AARCH64_TOOLCHAIN_ROOT is not set")
endif()
set(AARCH64_TOOLCHAIN_ROOT "$ENV{AARCH64_TOOLCHAIN_ROOT}" CACHE PATH "Cross toolchain root")

# binutils in the cross packages resolves libbfd/libopcodes from this directory.
set(ENV{LD_LIBRARY_PATH} "${AARCH64_TOOLCHAIN_ROOT}/usr/lib/x86_64-linux-gnu:$ENV{LD_LIBRARY_PATH}")

set(CMAKE_C_COMPILER "${AARCH64_TOOLCHAIN_ROOT}/usr/bin/aarch64-linux-gnu-gcc-11")
set(CMAKE_CXX_COMPILER "${AARCH64_TOOLCHAIN_ROOT}/usr/bin/aarch64-linux-gnu-g++-11")
set(CMAKE_SYSROOT "${AARCH64_TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH "${AARCH64_TOOLCHAIN_ROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

find_program(AARCH64_QEMU qemu-aarch64)
if(AARCH64_QEMU)
  set(CMAKE_CROSSCOMPILING_EMULATOR
      "${AARCH64_QEMU};-L;${AARCH64_TOOLCHAIN_ROOT}/usr/aarch64-linux-gnu")
endif()
