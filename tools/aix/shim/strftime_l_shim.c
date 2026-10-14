/* strftime_l_shim.c -- Projekt btop-aix (2026-10-12)
 *
 * Zweck: AIX 7.1 exportiert kein strftime_l(3); die libc++-Laufzeit von
 * IBM Open XL 17.1.3 (libc++.a[libc++.so.1], gebaut fuer AIX 7.2 TL5)
 * importiert es aus libc.a(shr_64.o). Ohne das Symbol verweigert der
 * AIX-Loader auf 7.1 das Laden jedes C++-Programms, das diese Laufzeit
 * nutzt (gemessen 2026-10-12 auf einem 7.1-Host, 7100-05-03: "Symbol strftime_l
 * (number 217) is not exported from dependent module libc.a[shr_64.o]").
 *
 * Diese Shared Library stellt strftime_l ueber uselocale(3)+strftime(3)
 * bereit und wird per LDR_PRELOAD64 vor allen anderen Modulen geladen;
 * der Loader loest Importe zuerst gegen Preload-Module auf.
 *
 * Bauen (AIX-7.2-Build-Host, Open XL, 64 Bit):
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -fPIC -shared \
 *     -Wl,-bE:strftime_l_shim.exp -Wl,-bnoentry -o libstrftime_l_shim.so strftime_l_shim.c
 * Einsatz (AIX 7.1):
 *   LDR_PRELOAD64=/opt/btop/lib/libstrftime_l_shim.so LIBPATH=/opt/btop/lib:/usr/lib:/lib btop
 *
 * Auf AIX 7.2/7.3 wird die Bibliothek nicht gebraucht (libc hat strftime_l).
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
