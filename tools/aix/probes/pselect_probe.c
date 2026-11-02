/* pselect_probe.c -- project btop-aix (2026-10-19)
 *
 * Who/what: Measures how long pselect() on STDIN (a terminal without input) really waits for several timeouts and
 *           compares it with select(). Output per case: timeout, return value, errno, measured ms.
 * Why:      truss on AIX 7100-02 and 6100-09 showed that btop's Input::poll called pselect with less than 1000 ms left
 *           (tv_sec=0, tv_nsec>0); _select then returned 0 at once, for one second of every 2-s cycle. Assumption:
 *           pselect on AIX handles tv_sec=0 wrongly. This probe checks that in isolation. Result: pselect on AIX 6.1
 *           and 7.1 ignores tv_nsec; on 7.2 it works.
 * Build:    (AIX 7.2 build host with Open XL) /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O1 -o pselect_probe pselect_probe.c
 *           ./pselect_probe </dev/tty  (under ssh -tt, so that STDIN is a terminal without input)
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void probe_pselect(long sec, long nsec) {
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(0, &fds);
	struct timespec ts = {sec, nsec};
	sigset_t mask;
	sigemptyset(&mask);
	double t0 = now_ms();
	int rc = pselect(1, &fds, NULL, NULL, &ts, &mask);
	int e = errno;
	printf("pselect tv_sec=%ld tv_nsec=%-10ld rc=%d errno=%-3d (%s) gewartet %8.1f ms\n", sec, nsec, rc, rc < 0 ? e : 0,
		   rc < 0 ? strerror(e) : "-", now_ms() - t0);
}

static void probe_select(long sec, long usec) {
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(0, &fds);
	struct timeval tv = {sec, usec};
	double t0 = now_ms();
	int rc = select(1, &fds, NULL, NULL, &tv);
	printf("select  tv_sec=%ld tv_usec=%-10ld rc=%d gewartet %8.1f ms\n", sec, usec, rc, now_ms() - t0);
}

int main(void) {
	/* Faelle wie in btops Input::poll(min(1000, rest)): 1000 ms, 999 ms, 500 ms, 10 ms, 1 ms */
	probe_pselect(1, 0);
	probe_pselect(0, 999000000L);
	probe_pselect(0, 500000000L);
	probe_pselect(0, 10000000L);
	probe_pselect(0, 1000000L);
	probe_pselect(1, 500000000L);
	probe_select(0, 500000L);
	probe_select(1, 0);
	return 0;
}
