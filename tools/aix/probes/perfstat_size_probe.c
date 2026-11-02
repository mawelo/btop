/* perfstat_size_probe.c -- project btop-aix (2026-10-12)
 *
 * What : For each libperfstat call btop uses, try the structure size the build headers define
 *        (sizeof) and, when the library rejects it, shrink the size in steps of 8 bytes until the
 *        call succeeds. Prints the accepted size and errno of the first failure. Plain C so that
 *        it can be built against the 7.1 sysroot without the C++ runtime.
 * Why  : On AIX 7100-02 (libperfstat 7.1.2) btop built against the 7100-05 sysroot showed an empty
 *        CPU box: the perfstat_cpu_total_t of 7100-05 is larger than the library knows, the call
 *        fails with EINVAL and btop's CPU collection returns early.
 * Build: /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 --sysroot=/build/btop-aix/sysroot71 \
 *            -L/build/btop-aix/sysroot71/usr/lib -O1 -o perfstat_size_probe perfstat_size_probe.c -lperfstat
 * Run  : ./perfstat_size_probe   (on the host under test)
 */
#include <errno.h>
#include <libperfstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*probe_fn)(void *buf, size_t size);

static int cpu_total(void *b, size_t s)  { return perfstat_cpu_total(NULL, (perfstat_cpu_total_t *)b, s, 1); }
static int mem_total(void *b, size_t s)  { return perfstat_memory_total(NULL, (perfstat_memory_total_t *)b, s, 1); }
static int cpu_first(void *b, size_t s)  { perfstat_id_t id; strcpy(id.name, FIRST_CPU); return perfstat_cpu(&id, (perfstat_cpu_t *)b, s, 1); }
static int disk_first(void *b, size_t s) { perfstat_id_t id; strcpy(id.name, FIRST_DISK); return perfstat_disk(&id, (perfstat_disk_t *)b, s, 1); }
static int net_first(void *b, size_t s)  { perfstat_id_t id; strcpy(id.name, FIRST_NETINTERFACE); return perfstat_netinterface(&id, (perfstat_netinterface_t *)b, s, 1); }
static int lv_first(void *b, size_t s)   { perfstat_id_t id; strcpy(id.name, FIRST_LOGICALVOLUME); return perfstat_logicalvolume(&id, (perfstat_logicalvolume_t *)b, s, 1); }

static void probe(const char *name, probe_fn fn, size_t native) {
	char *buf = calloc(1, native + 64);
	errno = 0;
	int rc = fn(buf, native);
	if (rc >= 1) {
		printf("%-26s sizeof=%zu  OK (rc=%d)\n", name, native, rc);
	} else {
		int first_errno = errno;
		size_t s = native;
		int ok = 0;
		while (s > 64) {
			s -= 8;
			errno = 0;
			memset(buf, 0, native + 64);
			if (fn(buf, s) >= 1) { ok = 1; break; }
		}
		if (ok) printf("%-26s sizeof=%zu  FAILS (rc=%d errno=%d %s); accepted size <= %zu\n", name, native, rc, first_errno, strerror(first_errno), s);
		else    printf("%-26s sizeof=%zu  FAILS (rc=%d errno=%d %s); no smaller size accepted\n", name, native, rc, first_errno, strerror(first_errno));
	}
	free(buf);
}

int main(void) {
	probe("perfstat_cpu_total",     cpu_total,  sizeof(perfstat_cpu_total_t));
	probe("perfstat_cpu",           cpu_first,  sizeof(perfstat_cpu_t));
	probe("perfstat_memory_total",  mem_total,  sizeof(perfstat_memory_total_t));
	probe("perfstat_disk",          disk_first, sizeof(perfstat_disk_t));
	probe("perfstat_netinterface",  net_first,  sizeof(perfstat_netinterface_t));
	probe("perfstat_logicalvolume", lv_first,   sizeof(perfstat_logicalvolume_t));
	return 0;
}
