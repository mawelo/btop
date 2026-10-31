# SPDX-License-Identifier: Apache-2.0
#
# Toolchain file for building btop on AIX 7.2 TL5+ / 7.3 with IBM Open XL C/C++
# 17.1.3 (clang 19, libc++ 19). Native build, so CMAKE_SYSTEM_NAME is left to
# CMake (setting it here would flag the build as cross compiling).
#
# Usage on the AIX host:
#   OBJECT_MODE=64 cmake --preset aix-openxl
#   cmake --build --preset aix-openxl
#
# Override the compiler location with -DBTOP_OPENXL_ROOT=/opt/IBM/openxlC/17.1.4
# if a different Open XL level is installed.

set(BTOP_OPENXL_ROOT "/opt/IBM/openxlC/17.1.3" CACHE PATH "IBM Open XL C/C++ installation root")

set(CMAKE_C_COMPILER   "${BTOP_OPENXL_ROOT}/bin/ibm-clang_r")
set(CMAKE_CXX_COMPILER "${BTOP_OPENXL_ROOT}/bin/ibm-clang++_r")

# 64-bit objects everywhere (the Open XL default is 32-bit unless OBJECT_MODE=64)
set(CMAKE_C_FLAGS_INIT          "-m64")
set(CMAKE_CXX_FLAGS_INIT        "-m64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m64")

# AIX shared libraries live inside .a archives; never pick up stray .so files
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
