// perfstat_sizes.cpp -- Projekt btop-aix (2026-10-12)
// Zweck: Groessen der libperfstat-Strukturen des Build-Hosts ausgeben und
// die perfstat-Aufrufe mit genau dieser Groesse sowie mit kleineren
// (aelteren) Groessen probieren. Dient dem Nachweis, ob eine aeltere
// libperfstat (AIX 7.1) eine groessere sizeof_userbuff akzeptiert.
// Bauen (AIX-7.2-Build-Host mit Open XL): /opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r -m64 -std=c++23 -O1
//                  -o perfstat_sizes perfstat_sizes.cpp -lperfstat
// Aufruf: ./perfstat_sizes            (auf 7.2 und auf 7.1 laufen lassen)
#include <libperfstat.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

template <class T, class F>
static void probe(const char* what, F fn, size_t native, size_t older) {
	T buf{};
	errno = 0;
	int rc = fn(&buf, native);
	std::printf("%-26s sizeof=%5zu rc=%d errno=%d", what, native, rc, errno);
	if (older && older != native) {
		std::memset(&buf, 0, sizeof buf);
		errno = 0;
		rc = fn(&buf, older);
		std::printf(" | older=%5zu rc=%d errno=%d", older, rc, errno);
	}
	std::printf("\n");
}

int main(int argc, char** argv) {
	// Optionale "aeltere" Groessen (7.1) als Argumente: cpu_total cpu memory_total disk netinterface
	size_t o[5] = {0, 0, 0, 0, 0};
	for (int i = 1; i < argc && i <= 5; ++i) o[i - 1] = std::strtoul(argv[i], nullptr, 10);

	std::printf("sizeof perfstat_cpu_total_t    = %zu\n", sizeof(perfstat_cpu_total_t));
	std::printf("sizeof perfstat_cpu_t          = %zu\n", sizeof(perfstat_cpu_t));
	std::printf("sizeof perfstat_memory_total_t = %zu\n", sizeof(perfstat_memory_total_t));
	std::printf("sizeof perfstat_disk_t         = %zu\n", sizeof(perfstat_disk_t));
	std::printf("sizeof perfstat_netinterface_t = %zu\n", sizeof(perfstat_netinterface_t));
	std::printf("sizeof perfstat_pagingspace_t  = %zu\n", sizeof(perfstat_pagingspace_t));
	std::printf("sizeof perfstat_id_t           = %zu\n", sizeof(perfstat_id_t));

	probe<perfstat_cpu_total_t>("perfstat_cpu_total", [](perfstat_cpu_total_t* b, size_t s) { return perfstat_cpu_total(nullptr, b, (int)s, 1); }, sizeof(perfstat_cpu_total_t), o[0]);
	probe<perfstat_memory_total_t>("perfstat_memory_total", [](perfstat_memory_total_t* b, size_t s) { return perfstat_memory_total(nullptr, b, (int)s, 1); }, sizeof(perfstat_memory_total_t), o[2]);
	perfstat_id_t first{};
	std::strcpy(first.name, FIRST_CPU);
	probe<perfstat_cpu_t>("perfstat_cpu(first)", [&](perfstat_cpu_t* b, size_t s) { return perfstat_cpu(&first, b, (int)s, 1); }, sizeof(perfstat_cpu_t), o[1]);
	perfstat_id_t fd{};
	std::strcpy(fd.name, FIRST_DISK);
	probe<perfstat_disk_t>("perfstat_disk(first)", [&](perfstat_disk_t* b, size_t s) { return perfstat_disk(&fd, b, (int)s, 1); }, sizeof(perfstat_disk_t), o[3]);
	perfstat_id_t fn{};
	std::strcpy(fn.name, FIRST_NETINTERFACE);
	probe<perfstat_netinterface_t>("perfstat_netinterface(first)", [&](perfstat_netinterface_t* b, size_t s) { return perfstat_netinterface(&fn, b, (int)s, 1); }, sizeof(perfstat_netinterface_t), o[4]);

	perfstat_cpu_total_t ct{};
	if (perfstat_cpu_total(nullptr, &ct, sizeof ct, 1) == 1) {
		std::printf("ncpus=%d ncpus_cfg=%d desc='%s' HZ=%llu lbolt=%ld loadavg=%.2f %.2f %.2f\n",
			ct.ncpus, ct.ncpus_cfg, ct.description, ct.processorHZ, (long)ct.lbolt,
			ct.loadavg[0] / 65536.0, ct.loadavg[1] / 65536.0, ct.loadavg[2] / 65536.0);
	}
	std::printf("count cpu=%d disk=%d net=%d pagingspace=%d\n",
		perfstat_cpu(nullptr, nullptr, sizeof(perfstat_cpu_t), 0),
		perfstat_disk(nullptr, nullptr, sizeof(perfstat_disk_t), 0),
		perfstat_netinterface(nullptr, nullptr, sizeof(perfstat_netinterface_t), 0),
		perfstat_pagingspace(nullptr, nullptr, sizeof(perfstat_pagingspace_t), 0));
	return 0;
}
