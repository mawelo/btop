/* aix71_compat.h -- project btop-aix (2026-10-12)
 *
 * Who/what: Prepended via "-include" when building libc++/libc++abi/libunwind (LLVM 19.1.7)
 * and btop against the AIX 7.1 sysroot. AIX 7.1 (bos.rte
 * 7.1.5.31) has no strftime_l(3); libc++ calls it in src/locale.cpp.
 * The function is emulated here as static inline through uselocale(3)+strftime(3),
 * so that no import from libc arises (measured 2026-10-12:
 * strftime_l is the only symbol of the libc++ 19 runtime that 7.1 lacks).
 *
 * Include only for builds against the 7.1 sysroot: Open XL defines _AIX72
 * according to the build host, not the sysroot, hence no macro guard.
 * On AIX 7.2 and later, <time.h> declares strftime_l itself.
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

/* The 7.1 <stdlib.h> declares abort/exit/_Exit without noreturn; libunwind and
 * libc++ build with -Werror=return-type and need the guarantee.
 * Repeating the declaration with a GNU attribute is permitted. */
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
