# Cross-compile for 64-bit ARM Linux.
#
# Two ways to supply the toolchain:
#
# 1. A packaged cross compiler on PATH (aarch64-linux-gnu-g++, for example from
#    `apt install g++-aarch64-linux-gnu`). Nothing else is needed:
#
#      cmake -S . -B build/aarch64 \
#            -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
#
# 2. A directory whose layout matches a Debian root: usr/bin/aarch64-linux-gnu-g++-11
#    and usr/aarch64-linux-gnu/lib must exist. This is how the Ubuntu cross
#    packages can be used without root by unpacking them.
#
#      export AARCH64_TOOLCHAIN_ROOT=/opt/cross-aarch64/root
#      cmake -S . -B build/aarch64 \
#            -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
#
# CTest runs the binaries through qemu-aarch64 when it is installed.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

if(DEFINED ENV{AARCH64_TOOLCHAIN_ROOT})
  set(AARCH64_TOOLCHAIN_ROOT "$ENV{AARCH64_TOOLCHAIN_ROOT}" CACHE PATH "Cross toolchain root")

  # binutils in the unpacked packages resolve libbfd/libopcodes from this directory.
  set(ENV{LD_LIBRARY_PATH}
      "${AARCH64_TOOLCHAIN_ROOT}/usr/lib/x86_64-linux-gnu:$ENV{LD_LIBRARY_PATH}")

  set(CMAKE_CXX_COMPILER "${AARCH64_TOOLCHAIN_ROOT}/usr/bin/aarch64-linux-gnu-g++-11")
  set(CMAKE_SYSROOT "${AARCH64_TOOLCHAIN_ROOT}")
  set(CMAKE_FIND_ROOT_PATH "${AARCH64_TOOLCHAIN_ROOT}")
  set(_someip_qemu_sysroot "${AARCH64_TOOLCHAIN_ROOT}/usr/aarch64-linux-gnu")
else()
  find_program(AARCH64_GXX NAMES aarch64-linux-gnu-g++)
  if(NOT AARCH64_GXX)
    message(FATAL_ERROR
        "aarch64-linux-gnu-g++ not found: install the cross toolchain or set "
        "AARCH64_TOOLCHAIN_ROOT")
  endif()
  set(CMAKE_CXX_COMPILER "${AARCH64_GXX}")
  set(_someip_qemu_sysroot "/usr/aarch64-linux-gnu")
endif()

set(SOMEIP_QEMU_SYSROOT "${_someip_qemu_sysroot}" CACHE PATH
    "Sysroot passed to qemu-aarch64 -L when CTest runs the binaries")

find_program(AARCH64_QEMU qemu-aarch64)
if(AARCH64_QEMU)
  set(CMAKE_CROSSCOMPILING_EMULATOR "${AARCH64_QEMU};-L;${SOMEIP_QEMU_SYSROOT}")
endif()
