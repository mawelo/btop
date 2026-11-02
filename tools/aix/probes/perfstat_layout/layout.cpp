// layout.cpp -- project btop-aix (2026-10-12): host-side analysis, NOT an AIX build.
// Prints sizeof and the offsets of the fields btop reads for one libperfstat header version
// (SAP-wrapped copies under .../src/libperfstat/includes, namespace PERFSTAT_<ver> or global).
// Build (Linux host, per version): clang++-22 -std=c++20 -m64 -x c++ -w -I tools/aix/probes/perfstat_layout -isystem tools/aix/probes/perfstat_layout/stub
//        -DHDR='"<path>/libperfstat_<ver>.h"' [-DNS=PERFSTAT_<ver>] -o layout layout.cpp
#include "preamble.h"
typedef int cpu_t;
#include HDR
#include <cstdio>
#include <cstddef>
#ifdef NS
using namespace NS;
#endif
#define OFF(T, f) []<class U>(U *) { if constexpr (requires(U u) { u.f; }) printf("  %-22s %-14s off=%4zu size=%zu\n", #T, #f, offsetof(U, f), sizeof(U::f)); else printf("  %-22s %-14s (not in this version)\n", #T, #f); }((T *)nullptr)
int main() {
	printf("sizeof: cpu_total=%zu cpu=%zu memory_total=%zu disk=%zu netinterface=%zu",
	       sizeof(perfstat_cpu_total_t), sizeof(perfstat_cpu_t), sizeof(perfstat_memory_total_t), sizeof(perfstat_disk_t), sizeof(perfstat_netinterface_t));
	if constexpr (requires { sizeof(perfstat_logicalvolume_t); }) printf(" logicalvolume=%zu\n", sizeof(perfstat_logicalvolume_t)); else printf(" logicalvolume=(none)\n");
	OFF(perfstat_cpu_total_t, ncpus); OFF(perfstat_cpu_total_t, description); OFF(perfstat_cpu_total_t, processorHZ); OFF(perfstat_cpu_total_t, loadavg);
	OFF(perfstat_cpu_total_t, user); OFF(perfstat_cpu_total_t, sys); OFF(perfstat_cpu_total_t, idle); OFF(perfstat_cpu_total_t, wait); OFF(perfstat_cpu_total_t, lbolt);
	OFF(perfstat_cpu_t, name); OFF(perfstat_cpu_t, user); OFF(perfstat_cpu_t, sys); OFF(perfstat_cpu_t, idle); OFF(perfstat_cpu_t, wait);
	OFF(perfstat_memory_total_t, real_total); OFF(perfstat_memory_total_t, real_free); OFF(perfstat_memory_total_t, real_inuse); OFF(perfstat_memory_total_t, numperm);
	OFF(perfstat_memory_total_t, pgsp_total); OFF(perfstat_memory_total_t, pgsp_free); OFF(perfstat_memory_total_t, real_avail);
	OFF(perfstat_disk_t, name); OFF(perfstat_disk_t, description); OFF(perfstat_disk_t, vgname); OFF(perfstat_disk_t, size); OFF(perfstat_disk_t, free); OFF(perfstat_disk_t, bsize);
	OFF(perfstat_disk_t, xfers); OFF(perfstat_disk_t, wblks); OFF(perfstat_disk_t, rblks); OFF(perfstat_disk_t, qdepth); OFF(perfstat_disk_t, time);
	OFF(perfstat_netinterface_t, name); OFF(perfstat_netinterface_t, ibytes); OFF(perfstat_netinterface_t, obytes);
	if constexpr (requires { sizeof(perfstat_logicalvolume_t); }) { OFF(perfstat_logicalvolume_t, name); OFF(perfstat_logicalvolume_t, kbreads); OFF(perfstat_logicalvolume_t, kbwrites); }
	return 0;
}
