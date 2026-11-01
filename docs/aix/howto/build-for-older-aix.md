# Build btop for AIX 7.1 and 6.1

Applies to: presets `aix71-openxl` and `aix61-openxl`, IBM Open XL C/C++ 17.1.3, LLVM 19.1.7
Verified: one-time setup on 2026-10-12; preset build in every CI run of btop-aix    Revision: 1

## Goal

At the end, `build/aix71-openxl/btop` (or `build/aix61-openxl/btop`) exists. The binary imports only `libc.a`,
`libpthreads.a` and `libperfstat.a` and runs on AIX 7.1 (or 6.1). The reasons for each step are in
[Why btop for AIX 7.1 and 6.1 needs a sysroot](../explanation/older-aix-releases.md).

## Prerequisites

- A build host with AIX 7.2 TL5 or 7.3 and IBM Open XL C/C++ 17.1.3 in `/opt/IBM/openxlC/17.1.3`.
- `cmake` >= 3.25 (the C++26 build was tested with 4.2.0), `ninja-build` or GNU `make`, `perl` and `patch`.
- A host with the target release (7.1 or 6.1). An ordinary user account is enough.
- The LLVM 19.1.7 sources `llvm-project-19.1.7.src.tar.xz` from the LLVM releases on GitHub, sha256
  `82401fea7b79d0078043f7598b835284d6650a75b93e64b6f761ea7b63097501`.
- A directory `/build/btop-aix` on the build host, which the presets expect. For another directory, pass `-DBTOP_AIX_SYSROOT=…`, `-DBTOP_AIX_RUNTIME=…` and `-DBTOP_AIX_COMPAT=…` to `cmake`.

The steps use AIX 7.1 and the suffix `71`. For AIX 6.1, use `61` and the additional flags in step 4.

## Steps

### 1. Pack the sysroot on the target host

The tar file must contain `usr/include`, `usr/ccs/lib`, all `usr/lib/*.o`, `*.exp` and `*.imp`, and the
libraries that btop links from `usr/lib` (`libc.a`, `libpthreads.a`, `libperfstat.a` and their links).
`libC.a` from the fileset `xlC.aix61.rte` goes into a second tar file. Pack symbolic links as links: do not use
`tar -h`.

untested: the command below was not run in exactly this form.

```sh
cd / && tar -cf - usr/include usr/ccs/lib usr/lib/*.o usr/lib/*.exp usr/lib/*.imp \
    usr/lib/libc.a usr/lib/libpthreads.a usr/lib/libperfstat.a | gzip > $HOME/sysroot71.tar.gz
```

Copy the tar files to `/build/btop-aix/` on the build host.

Success: `gzip -dc sysroot71.tar.gz | tar -tf - | grep pthread.h` lists `usr/include/pthread.h`.

### 2. Create the sysroot on the build host

```sh
cd <BTOP_SOURCE>/tools/aix/sysroot71
perl make_sysroot.pl --root /build/btop-aix/sysroot71 --tag aix71 \
     --tar /build/btop-aix/sysroot71.tar.gz --tar /build/btop-aix/sysroot71-libC.tar.gz
perl make_cxxwrap.pl --sysroot /build/btop-aix/sysroot71
cp aix71_compat.h /build/btop-aix/aix71_compat.h
```

For 6.1, pass only the one tar file and use `aix61_compat.h`.

Success: `/build/btop-aix/sysroot71/usr/include/pthread.h` and `/build/btop-aix/sysroot71/cxxwrap/ctype.h`
exist.

### 3. Patch the LLVM sources

```sh
cd /build/btop-aix
xz -dc llvm-project-19.1.7.src.tar.xz | tar -xf -
cd llvm-project-19.1.7.src
patch -p0 < <BTOP_SOURCE>/tools/aix/sysroot71/llvm-19.1.7-aix-cmake-quote.patch
patch -p0 < <BTOP_SOURCE>/tools/aix/sysroot71/llvm-19.1.7-aix-cxa-thread-atexit.patch
```

Success: `patch` reports no rejected hunks.

### 4. Build the static libc++

```sh
perl <BTOP_SOURCE>/tools/aix/sysroot71/build_rt.pl --src /build/btop-aix/llvm-project-19.1.7.src \
     --sysroot /build/btop-aix/sysroot71 --prefix /build/btop-aix/rt71 \
     --build /build/btop-aix/build-rt71 --compat /build/btop-aix/aix71_compat.h
```

For 6.1, add:

```sh
     --extra-flags "-fno-builtin-strlen -fno-builtin-strcmp -DREMOVE_ALL_USE_DIRECTORY_ITERATOR"
```

Success: `/build/btop-aix/rt71/lib/libc++.a` exists.

### 5. Build btop

```sh
export PATH=/opt/freeware/bin:$PATH OBJECT_MODE=64
cd <BTOP_SOURCE>
cmake --preset aix71-openxl
cmake --build --preset aix71-openxl -j 8
```

Success: `build/aix71-openxl/btop --version` prints the version and the commit ID.

## Verify

The check compares the imports of the binary with the exports of the target host. The CI of btop-aix runs the
import list and the comparison after each build, with export lists of 7100-05-03 and 6100-09-04.

On the target host, create the export list. untested: this exact command was not run; the head of
`tools/aix/check_imports.pl` describes the form.

```sh
dump -X64 -Tv /usr/lib/libc.a /usr/lib/libpthreads.a /usr/lib/libperfstat.a \
     | awk '$4=="EXP"{print $NF}' | sort -u > libc_exports_<LEVEL>.txt
```

On the build host, create the import list and compare:

```sh
dump -X64 -Tv build/aix71-openxl/btop | awk '$4=="IMP"{print $NF}' | sort -u > imports-aix71.txt
perl tools/aix/check_imports.pl --imports imports-aix71.txt --exports libc_exports_<LEVEL>.txt
```

The check passes with exit code 0, which means that no symbol is missing. On the target host, start the binary
with `TERM=xterm-256color` and either `LANG=en_US.UTF-8` or `--force-utf`.

## If it fails

| Message | Cause | Fix |
|---|---|---|
| `_Z7isdigiti undefined` | the C++ wrappers of the C headers are missing | run `make_cxxwrap.pl` again (step 2) |
| `'abs' is missing exception specification` | `math.h` in the sysroot lacks the guard | run `make_sysroot.pl` again on fresh tar files (step 2) |
| `getFuncNameFromTBTable` undefined while linking | LLVM sources without the CMake patch | apply the patches (step 3) and rebuild the runtime (step 4) |
| on the target host: `Symbol quick_exit … is not exported` | binary from a btop source older than commit `ab9e608` | build from a current source |

## Related

- [Why btop for AIX 7.1 and 6.1 needs a sysroot](../explanation/older-aix-releases.md)
- [AIX porting pitfalls](../reference/porting-pitfalls.md)
- `cmake/Toolchain-aix-sysroot-openxl.cmake` (cache variables of the presets)
