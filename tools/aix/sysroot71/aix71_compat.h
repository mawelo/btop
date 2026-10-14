/* aix71_compat.h -- Projekt btop-aix (2026-10-12)
 *
 * Zweck: Wird beim Bau von libc++/libc++abi/libunwind (LLVM 19.1.7) und von
 * btop gegen das AIX-7.1-Sysroot per "-include" vorangestellt. AIX 7.1 (bos.rte
 * 7.1.5.31) kennt kein strftime_l(3); libc++ ruft es in src/locale.cpp auf.
 * Die Funktion wird hier als static inline ueber uselocale(3)+strftime(3)
 * nachgebildet, so dass kein Import aus libc entsteht (gemessen 2026-10-12:
 * strftime_l ist das einzige Symbol der libc++-19-Laufzeit, das 7.1 fehlt).
 *
 * Nur fuer Builds gegen das 7.1-Sysroot einbinden: Open XL definiert _AIX72
 * nach dem Build-Host, nicht nach dem Sysroot, daher kein Makro-Schutz.
 * Auf AIX 7.2 und spaeter deklariert <time.h> strftime_l selbst.
 */
#ifndef BTOP_AIX71_COMPAT_H
#define BTOP_AIX71_COMPAT_H

#if defined(_AIX)

#include <locale.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Die 7.1-<stdlib.h> deklariert abort/exit/_Exit ohne noreturn; libunwind und
 * libc++ bauen mit -Werror=return-type und brauchen die Zusicherung.
 * Eine Wiederholung der Deklaration mit GNU-Attribut ist zulaessig. */
extern void abort(void) __attribute__((__noreturn__));
extern void exit(int) __attribute__((__noreturn__));
extern void _Exit(int) __attribute__((__noreturn__));

static inline size_t strftime_l(char *__buf, size_t __max, const char *__fmt,
                                const struct tm *__tm, locale_t __loc)
{
	locale_t __old = uselocale(__loc);
	size_t __n = strftime(__buf, __max, __fmt, __tm);
	if (__old != (locale_t)0)
		uselocale(__old);
	return __n;
}

#ifdef __cplusplus
}
#endif

#endif /* _AIX */
#endif /* BTOP_AIX71_COMPAT_H */
