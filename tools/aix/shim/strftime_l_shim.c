/* strftime_l_shim.c -- project btop-aix (2026-10-12)
 *
 * Who/what: AIX 7.1 does not export strftime_l(3); the libc++ runtime of
 * IBM Open XL 17.1.3 (libc++.a[libc++.so.1], built for AIX 7.2 TL5)
 * imports it from libc.a(shr_64.o). Without the symbol, the
 * AIX loader on 7.1 refuses to load any C++ program that uses this runtime
 * (measured 2026-10-12 on a 7.1 host, 7100-05-03: "Symbol strftime_l
 * (number 217) is not exported from dependent module libc.a[shr_64.o]").
 *
 * This shared library provides strftime_l through uselocale(3)+strftime(3)
 * and is loaded via LDR_PRELOAD64 before all other modules;
 * the loader resolves imports against preload modules first.
 *
 * Build (AIX 7.2 build host, Open XL, 64 bit):
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -fPIC -shared \
 *     -Wl,-bE:strftime_l_shim.exp -Wl,-bnoentry -o libstrftime_l_shim.so strftime_l_shim.c
 * Use (AIX 7.1):
 *   LDR_PRELOAD64=/opt/btop/lib/libstrftime_l_shim.so LIBPATH=/opt/btop/lib:/usr/lib:/lib btop
 *
 * On AIX 7.2/7.3 the library is not needed (libc has strftime_l).
 */
#include <locale.h>
#include <time.h>
#include <stddef.h>

size_t strftime_l(char *buf, size_t max, const char *fmt, const struct tm *tm, locale_t loc)
{
	locale_t old = uselocale(loc);
	size_t n = strftime(buf, max, fmt, tm);
	if (old != (locale_t)0)
		uselocale(old);
	return n;
}
