/* rusage_probe.c -- project btop-aix (2026-10-12)
 *
 * What : Shows the unit of procentry64.pi_ru.ru_utime/ru_stime.tv_usec as returned by getprocs64(3):
 *        AIX documents the field as microseconds like struct timeval, but the kernel stores
 *        nanoseconds there. A child burns CPU for ~2 s; the parent samples the child's entry
 *        every 500 ms and prints tv_sec, tv_usec and the derived CPU time under both readings.
 *        It also prints the delta of perfstat_cpu_total user+sys+idle+wait over the same window,
 *        which btop uses as the "all CPUs" denominator, and ncpus.
 * Why  : btop showed 1035 % and 3200 % for single-threaded busy loops on a 32-CPU LPAR.
 * Build: /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -D_ALL_SOURCE -O1 -o rusage_probe rusage_probe.c -lperfstat
 * Run  : ./rusage_probe
 */
#include <errno.h>
#include <libperfstat.h>
#include <procinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


static int fetch(pid_t pid, struct procentry64 *pe) {
	pid_t idx = pid;
	int n = getprocs64(pe, sizeof *pe, NULL, 0, &idx, 1);
	return (n == 1 && pe->pi_pid == pid) ? 0 : -1;
}

static unsigned long long total_ticks(void) {
	perfstat_cpu_total_t ct;
	memset(&ct, 0, sizeof ct);
	if (perfstat_cpu_total(NULL, &ct, sizeof ct, 1) != 1) return 0;
	return ct.user + ct.sys + ct.idle + ct.wait;
}

int main(void) {
	pid_t child = fork();
	if (child < 0) { perror("fork"); return 1; }
	if (child == 0) {
		alarm(3);
		for (volatile unsigned long i = 0;; i++) { }
	}
	perfstat_cpu_total_t ct;
	memset(&ct, 0, sizeof ct);
	perfstat_cpu_total(NULL, &ct, sizeof ct, 1);
	printf("ncpus=%d clk_tck=%ld\n", ct.ncpus, sysconf(_SC_CLK_TCK));
	unsigned long long t0 = total_ticks();
	for (int i = 0; i < 4; i++) {
		usleep(500000);
		struct procentry64 pe;
		if (fetch(child, &pe) != 0) { printf("sample %d: child gone\n", i); break; }
		long long us_sec = pe.pi_ru.ru_utime.tv_sec, us_frac = pe.pi_ru.ru_utime.tv_usec;
		long long ss_sec = pe.pi_ru.ru_stime.tv_sec, ss_frac = pe.pi_ru.ru_stime.tv_usec;
		double as_usec = us_sec + us_frac / 1e6 + ss_sec + ss_frac / 1e6;
		double as_nsec = us_sec + us_frac / 1e9 + ss_sec + ss_frac / 1e9;
		printf("sample %d: utime=%lld.%09lld stime=%lld.%09lld  cpu(if usec)=%.3f s  cpu(if nsec)=%.3f s  perfstat ticks delta=%llu\n",
		       i, us_sec, us_frac, ss_sec, ss_frac, as_usec, as_nsec, total_ticks() - t0);
	}
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	return 0;
}
