# Why btop for AIX 7.1 and 6.1 needs a sysroot and its own libc++

Applies to: presets `aix71-openxl` and `aix61-openxl`, IBM Open XL C/C++ 17.1.3, LLVM 19.1.7    Revision: 1

IBM Open XL C/C++ runs on AIX 7.2 TL5 and 7.3 only. The build of a binary for AIX 7.1 or 6.1 therefore runs on a
7.2 host. This document explains why such a binary needs a sysroot of the target release and a static libc++ that
is built against this sysroot. The steps are in
[Build btop for AIX 7.1 and 6.1](../howto/build-for-older-aix.md).

## A 7.2 binary fails on 7.1 as soon as libc++ is involved

IBM guarantees binary compatibility only from an older release to a newer one. A binary from a 7.2 host runs on
7.1 only as long as no statically initialised mutex is involved. With libc++ that is the case at once.
Five probes showed this on 2026-10-12 on AIX 7100-05-03:

| Probe | Result on 7100-05-03 |
|---|---|
| perfstat calls with the structure sizes of 7.2 | runs, all calls return 1 |
| C++ program with the IBM libc++ 17.1.3, given by `LIBPATH` | the loader stops: `Symbol strftime_l … is not exported from libc.a[shr_64.o]` |
| the same program with a preloaded `strftime_l` shim (`tools/aix/shim/`) | loads, then aborts: `libc++abi: __cxa_guard_acquire failed to acquire mutex` |
| C program with 7.2 headers | `pthread_mutex_lock` on a `PTHREAD_MUTEX_INITIALIZER` mutex returns EINVAL |
| the same C program, built against a 7.1 sysroot | all calls return 0 |

There are two causes:

- In 64-bit mode `PTHREAD_MUTEX_INITIALIZER` is `{{0,0,0,0,2,0}}` on 7.1 and `{{0,2,0}}` on 7.2. The field
  layout differs, so the libpthreads of 7.1 rejects the initializer of 7.2. Threads and `pthread_once` work. Mutexes also
  work if the program initialises them at runtime.
- The libc of 7.1 has no `strftime_l`.

## A sysroot provides the headers of the target release

The compiler reads the headers and start files from a copy of the target system (sysroot). Every initializer and
every structure then has the layout of the target release. The sysroot of 7.1 needs three corrections, which
`tools/aix/sysroot71/make_sysroot.pl` makes:

- The 7.1 `crti_64.o` needs `_cdtors` and symbols of libC, which the 7.2 linker does not provide. The 7.2
  `crti_64.o` needs only `atexit`, `exit` and `__run_*_ctors`. The sysroot therefore contains the 7.2 file.
- `math.h` gets the guard `__LIBC_NO_CPP_MATH_OVERLOADS__` around its C++ overloads, as in 7.2. libc++ defines
  these functions itself; without the guard the compiler reports "'abs' is missing exception specification".
- Absolute symbolic links become relative. Otherwise they point to the files of the build host.

The C headers of 7.1 have no `extern "C"` brackets. Without them, `isdigit`, `catopen` and `socket` get C++
linkage, and the link fails with `_Z7isdigiti undefined`. `make_cxxwrap.pl` creates one wrapper per header.

The driver of Open XL applies `--sysroot` to headers and `crt0`, but not to `-l` options. The toolchain file
`cmake/Toolchain-aix-sysroot-openxl.cmake` therefore adds `-L<sysroot>/usr/lib` itself.

## A static libc++ avoids the IBM runtime

The IBM libc++ runtime is built for 7.2. btop therefore links its own libc++, libc++abi and libunwind from LLVM
19.1.7, built statically against the sysroot (`build_rt.pl`). The resulting binary imports only `libc.a`,
`libpthreads.a` and `libperfstat.a`. On 2026-10-12 the 7.1 binary imported 218 symbols. `tools/aix/check_imports.pl` showed that the libc of 7100-05-03
exports all of them.

LLVM 19.1.7 needs two source patches and one CMake option:

| Change | Reason |
|---|---|
| `llvm-19.1.7-aix-cmake-quote.patch` | `if(${CMAKE_SYSTEM_NAME} MATCHES "AIX")` is unquoted. On AIX, `AIX` is itself a CMake variable with the value 1, so the comparison fails, and libunwind misses `Unwind_AIXExtras.cpp`. |
| `llvm-19.1.7-aix-cxa-thread-atexit.patch` | libc++abi references `__cxa_thread_atexit_impl` as a weak symbol. XCOFF has no weak undefined symbols, and the libc of 7.1 lacks the function. The patch makes libc++abi use its own implementation. |
| `LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF` | The configure test finds the symbol in the libc of the 7.2 build host. |

The compatibility header `aix71_compat.h` adds `strftime_l` as a `static inline` function, so the binary does
not import it. It also declares `abort`, `exit` and `_Exit` as `noreturn`, because the 7.1 `stdlib.h` does not.
Open XL defines `_AIX72` after the build host and not after the sysroot. The header therefore must not depend on
that macro.

## AIX 6.1 needs four more answers

Against the libc exports of 6100-09-04, 34 of these 218 imports of the 7.1 binary are missing. The 6.1 build uses
the same sysroot method with these additions:

| Finding | Answer |
|---|---|
| The POSIX 2008 locale API is missing: `locale_t`, `newlocale`, `uselocale`, `freelocale` and 24 `*_l` functions. libc++ 19 requires them on AIX. | `aix61_compat.h` provides one global locale. `newlocale` returns a placeholder and never NULL, because `locale_byname` would throw. Each `*_l` function calls the function without locale. `std::locale("de_DE")` therefore creates no separate locale; btop needs only "C". |
| `openat`, `fdopendir` and `unlinkat` are missing; libc++ `<filesystem>` uses them. | libc++ is built with `-DREMOVE_ALL_USE_DIRECTORY_ITERATOR`, a code path without `openat`. |
| `___strlen64` and `___strcmp64` are missing. Open XL replaces `strlen` and `strcmp` in 64-bit objects with these libc routines. | The preset `aix61-openxl` passes `-fno-builtin-strlen -fno-builtin-strcmp -fno-builtin-strcpy`. `build_rt.pl` gets `-fno-builtin-strlen -fno-builtin-strcmp` through `--extra-flags`. |
| `<fcntl.h>` lacks `O_CLOEXEC`. | The compatibility header defines it as 0. btop starts no child processes. |

## Where the method ends

`quick_exit` exists in the libc only from 6.1 TL9 and 7.1 TL3 on. btop therefore calls `_Exit` on AIX; it
registers no `at_quick_exit` handlers. With this change the 7.1 and the 6.1 binary also load and run on 7100-02-01 (measured once each).

## Related

- [Build btop for AIX 7.1 and 6.1](../howto/build-for-older-aix.md)
- [AIX porting pitfalls](../reference/porting-pitfalls.md)
