/* pthread_probe.c -- project btop-aix (2026-10-12)
 * Who/what: Checks whether a program built on AIX 7.2 TL5 (Open XL 17.1.3) with a
 * statically initialized mutex (PTHREAD_MUTEX_INITIALIZER), a dynamically
 * initialized mutex and one thread runs on AIX 7.1. Reason: the
 * libc++abi runtime 17.1.3 aborts on 7100-05-03 with
 * "__cxa_guard_acquire failed to acquire mutex" (measured 2026-10-12, AIX 7100-05-03).
 * Build (AIX 7.2 build host with Open XL): /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O1 -pthread -o pthread_probe pthread_probe.c
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t static_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int once_ran = 0;
static void once_fn(void) { once_ran = 1; }
static void* thr(void* arg) { (void)arg; return (void*)42; }

int main(void)
{
	int rc;
	printf("sizeof(pthread_mutex_t)=%zu sizeof(pthread_cond_t)=%zu\n", sizeof(pthread_mutex_t), sizeof(pthread_cond_t));
	rc = pthread_mutex_lock(&static_mx);  printf("static  lock   rc=%d (%s)\n", rc, strerror(rc));
	rc = pthread_mutex_unlock(&static_mx); printf("static  unlock rc=%d (%s)\n", rc, strerror(rc));
	pthread_mutex_t dyn;
	rc = pthread_mutex_init(&dyn, NULL); printf("dynamic init   rc=%d (%s)\n", rc, strerror(rc));
	rc = pthread_mutex_lock(&dyn);  printf("dynamic lock   rc=%d (%s)\n", rc, strerror(rc));
	rc = pthread_mutex_unlock(&dyn); printf("dynamic unlock rc=%d (%s)\n", rc, strerror(rc));
	rc = pthread_once(&once, once_fn); printf("once rc=%d ran=%d\n", rc, once_ran);
	pthread_t t; void* ret = 0;
	rc = pthread_create(&t, NULL, thr, NULL); printf("create rc=%d (%s)\n", rc, strerror(rc));
	if (rc == 0) { rc = pthread_join(t, &ret); printf("join rc=%d ret=%ld\n", rc, (long)ret); }
	printf("OK\n");
	return 0;
}
