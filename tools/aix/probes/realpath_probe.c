/* realpath_probe.c -- project btop-aix (2026-10-19)
 *
 * Who/what: Calls realpath(path, NULL) (POSIX.1-2008: result allocated with malloc) and realpath(path, buf) for "."
 *           and "/tmp/../tmp" and prints the result and errno.
 * Why:      libc++ 19 std::filesystem::canonical uses realpath(p, nullptr) (operations.cpp, __canonical). btop
 *           --mcp-stdio did not find its tool registry on AIX 7200-05, 6100-09 and 6100-07 (Theme::theme_dir empty),
 *           but did on 7100-02 and 7300-04. Does this depend on the AIX level? Result: realpath(p, NULL) returns
 *           EINVAL on 6.1 TL7/TL9 and 7.2 TL5.
 * Build:    (AIX 7.2 build host with Open XL and the 6.1 sysroot) /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O1
 *           -Wall -Wextra --sysroot=<sysroot61> -L<sysroot61>/usr/lib -o realpath_probe_aix61 realpath_probe.c
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void probe(const char* path) {
	errno = 0;
	char* r = realpath(path, NULL);
	int e = errno;
	printf("realpath(\"%s\", NULL) = %s errno=%d (%s)\n", path, r ? r : "NULL", r ? 0 : e, r ? "-" : strerror(e));
	free(r);
	char buf[PATH_MAX + 1];
	errno = 0;
	char* b = realpath(path, buf);
	e = errno;
	printf("realpath(\"%s\", buf)  = %s errno=%d\n", path, b ? b : "NULL", b ? 0 : e);
}

int main(void) {
	probe(".");
	probe("/tmp/../tmp");
	return 0;
}
