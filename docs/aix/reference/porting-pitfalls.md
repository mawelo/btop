# AIX porting pitfalls

Applies to: btop-aix with IBM Open XL C/C++ 17.1.3, AIX 6.1 to 7.3    Revision: 1

The tables list the AIX pitfalls that the port of btop met, with the effect and the answer in the code. All
entries were observed while porting. For the build for 7.1 and 6.1, see
[Why btop for AIX 7.1 and 6.1 needs a sysroot](../explanation/older-aix-releases.md).

## Compiler and headers

| Pitfall | Effect | Answer |
|---|---|---|
| no `<ifaddrs.h>` and no `getifaddrs` in the libc | build stops in `btop_shared.hpp` | `src/aix/aix_ifaddrs.*` |
| no `pthread_timedjoin_np` | link error | `pthread_join`, as on macOS and BSD |
| no `getloadavg` | no load average | `perfstat_cpu_total_t::loadavg` |
| the macros `hz`, `SACTIVE`, `SBITS` and `SKPROC` from `<sys/m_param.h>` and `<sys/proc.h>` (through `<procinfo.h>`) | "expected unqualified-id" | renamed identifiers (`clock_str`, `AixProc::STATE_*`) |
| `-std=c++23` sets `__STRICT_ANSI__`; the AIX headers then hide `sa_len`, `SIOCGIFCONF` and `getprocs64` | compile errors | `-D_ALL_SOURCE`, which the C++26 build keeps |
| without `-m64` or `OBJECT_MODE=64`, Open XL builds 32-bit objects | wrong structure sizes | the toolchain file sets `-m64`; the presets set `OBJECT_MODE=64` |
| Open XL accepts `-fstack-clash-protection` but ignores it | one warning per file | the flag is not set on AIX |
| `quick_exit` exists in the libc only from 6.1 TL9 and 7.1 TL3 on | the loader stops: `Symbol quick_exit … is not exported` | `_Exit` on AIX; btop registers no `at_quick_exit` handlers |

## Libraries and kernel interfaces

| Pitfall | Effect | Answer |
|---|---|---|
| libperfstat rejects a `sizeof_struct` that is larger than the structure of the installed library (EINVAL). A binary built against the 7100-05 sysroot passes 640 bytes for `perfstat_cpu_t`, and libperfstat 7.1.2 (7100-02-01) accepts 616. Built against the 6100-09 headers, `perfstat_cpu_t` has 624 bytes, and the library of 6100-07 accepts 616. | the CPU box stays empty; `btop.log` reports `perfstat_cpu() failed: Invalid argument` | `AixPerfstat::Array<T>` probes the accepted size once, in steps of 8 bytes down to half the size. It then copies the entries with this stride from a raw buffer. This applies to `perfstat_cpu`, `perfstat_disk`, `perfstat_netinterface` and `perfstat_logicalvolume`. |
| `procentry64.pi_ru.ru_utime.tv_usec` and `ru_stime.tv_usec` hold nanoseconds, not microseconds | single-threaded loops showed 1035 % and 3200 % CPU on a host with 32 CPUs | btop divides by 1e9 |

The sizes do not only grow: `perfstat_netinterface_t` has 240 bytes in 6.1.8 and 224 in 7.1.0 and 7.1.1.
The probing is still enough, because every field that btop reads has the same offset in all versions and ends
within the smallest structure size. The one exception is `real_avail`, which exists from 6.1.7 on. Fields
beyond the accepted size read as zero. The file `tools/aix/probes/perfstat_layout/offsets_5.3-7.3.txt` lists
the offsets in the headers of 5.3.9.7, 6.1.2.8, 6.1.7.0, 6.1.8.0, 7.1.0.1, 7.1.1.1, 7.2.5.201 and 7.3.1.0.

The structures have these sizes in bytes, depending on the version:

| Structure | Sizes |
|---|---|
| `perfstat_cpu_t` | 528, 616, 624, 640 |
| `perfstat_netinterface_t` | 208, 224, 240 |
| `perfstat_cpu_total_t` | 624, 696 |
| `perfstat_memory_total_t` | 208, 352 |
| `perfstat_disk_t` | 472, 496 |
| `perfstat_logicalvolume_t` | 216, 224 |

The probes are `tools/aix/probes/perfstat_size_probe.c` (accepted sizes) and `tools/aix/probes/rusage_probe.c`
(unit of `tv_usec`). Both ran on 2026-10-12.

## Shell and tools on AIX

| Pitfall | Effect | Answer |
|---|---|---|
| AIX `find` has no `-maxdepth`; AIX `grep` has no `-o`, no `\|` and no `-A` | empty source lists and silently wrong results in scripts | `$(wildcard)` in the Makefile; `egrep` or `perl` in scripts |
| a background job in ksh gets `/dev/null` as stdin | btop prints "No tty detected!" | `./btop </dev/tty &` for recordings |
| the host often has no UTF-8 locale (`bos.loc.utf.EN_US` is rarely installed) | btop stops with "No UTF-8 locale detected!" | `LANG=en_US.UTF-8` (btop checks only the name) or `--force-utf` |

## Related

- [Build btop for AIX 7.1 and 6.1](../howto/build-for-older-aix.md)
- [Why btop for AIX 7.1 and 6.1 needs a sysroot](../explanation/older-aix-releases.md)
