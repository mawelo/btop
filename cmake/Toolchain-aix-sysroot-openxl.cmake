# SPDX-License-Identifier: Apache-2.0
#
# Toolchain file for building a btop binary that runs on an AIX release older than the
# build host (AIX 7.1 or 6.1). Build host: AIX 7.2 TL5+ with IBM Open XL C/C++ 17.1.3.
# The compiler is pointed at a sysroot made from an installation of the target release and
# at a libc++/libc++abi/libunwind built statically against that sysroot
# (tools/aix/sysroot71/make_sysroot.pl, make_cxxwrap.pl, build_rt.pl).
# Background and measurements: docs/aix-port.md, section 5 (7.1) and 7 (6.1).
#
# Cache variables (set by the presets aix71-openxl / aix61-openxl, override with -D):
#   BTOP_AIX_SYSROOT      root of the target sysroot (usr/include, usr/lib, usr/ccs/lib, cxxwrap)
#   BTOP_AIX_RUNTIME      install prefix of the static libc++ built against that sysroot
#   BTOP_AIX_COMPAT       header prepended to every translation unit (strftime_l, noreturn, 6.1: xlocale)
#   BTOP_AIX_EXTRA_FLAGS  extra compiler flags (6.1: -fno-builtin-strlen -fno-builtin-strcmp)

set(BTOP_OPENXL_ROOT     "/opt/IBM/openxlC/17.1.3"        CACHE PATH "IBM Open XL C/C++ installation root")
set(BTOP_AIX_SYSROOT     "/build/btop-aix/sysroot71"      CACHE PATH "sysroot of the target AIX release")
set(BTOP_AIX_RUNTIME     "/build/btop-aix/rt71"           CACHE PATH "static libc++ built against the sysroot")
set(BTOP_AIX_COMPAT      "/build/btop-aix/aix71_compat.h" CACHE FILEPATH "compatibility header for the target release")
set(BTOP_AIX_EXTRA_FLAGS ""                               CACHE STRING "extra compiler flags for the target release")

set(CMAKE_C_COMPILER   "${BTOP_OPENXL_ROOT}/bin/ibm-clang_r")
set(CMAKE_CXX_COMPILER "${BTOP_OPENXL_ROOT}/bin/ibm-clang++_r")

# __LIBC_NO_CPP_MATH_OVERLOADS__: the (patched) math.h must not add its own C++ overloads of
# abs/acos/..., libc++ 19 provides them (same define as LLVM's libcxx/cmake/caches/AIX.cmake)
set(_btop_sr_common "-m64 --sysroot=${BTOP_AIX_SYSROOT} -include ${BTOP_AIX_COMPAT} -D__LIBC_NO_CPP_MATH_OVERLOADS__ ${BTOP_AIX_EXTRA_FLAGS}")
set(CMAKE_C_FLAGS_INIT   "${_btop_sr_common}")
# -nostdinc++: the IBM libc++ headers of the host must not be seen, only our own libc++ 19 headers
# cxxwrap: extern "C" wrappers around the target's C headers (tools/aix/sysroot71/make_cxxwrap.pl)
set(CMAKE_CXX_FLAGS_INIT "${_btop_sr_common} -nostdinc++ -isystem ${BTOP_AIX_RUNTIME}/include/c++/v1 -isystem ${BTOP_AIX_SYSROOT}/cxxwrap")

# Link order: target libraries before the host's /usr/lib (the driver adds no -L for the sysroot),
# our static C++ runtime instead of the IBM one (-nostdlib++ drops the driver's -lc++ -lc++abi).
set(CMAKE_EXE_LINKER_FLAGS_INIT
  "-m64 --sysroot=${BTOP_AIX_SYSROOT} -L${BTOP_AIX_RUNTIME}/lib -L${BTOP_AIX_SYSROOT}/usr/lib -nostdlib++ ${BTOP_AIX_RUNTIME}/lib/libc++.a ${BTOP_AIX_RUNTIME}/lib/libc++abi.a ${BTOP_AIX_RUNTIME}/lib/libunwind.a")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
