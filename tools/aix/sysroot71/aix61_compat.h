/* aix61_compat.h -- project btop-aix (2026-10-12)
 *
 * Who/what: Prepended via "-include" when building libc++/libc++abi/libunwind (LLVM 19.1.7)
 * and btop against the AIX 6.1 sysroot. The AIX 6.1 libc
 * (bos.rte.libc 6.1.9.30, measured 2026-10-12 on 6100-09-04) lacks the
 * POSIX 2008 locale interface: no locale_t, no newlocale/uselocale/
 * freelocale, no *_l functions (isdigit_l, strcoll_l, towupper_l, ...).
 * libc++ 19 requires them on AIX (locale_base_api/ibm.h).
 *
 * Emulation here: one single, global locale. newlocale returns a
 * placeholder (never NULL, otherwise locale_byname throws), uselocale/freelocale do
 * nothing, every *_l function calls the variant without the locale argument. Consequence:
 * std::locale("de_DE") creates no locale of its own; number and time formats follow
 * the process locale set by setlocale(3). btop needs only the "C" locale.
 *
 * In addition, from aix71_compat.h: strftime_l (through the dummy uselocale here) and
 * the noreturn declarations of abort/exit/_Exit.
 * Also aligned_alloc through posix_memalign (missing before 6.1 TL9), so that the package loads from 6.1 TL7 on.
 *
 * Further 6.1 specifics live in the compiler flags (preset aix61-openxl,
 * build_rt.pl --extra-flags): -fno-builtin-strlen -fno-builtin-strcmp, because
 * Open XL otherwise references ___strlen64/___strcmp64, which 6.1 does not export
 * (probe tools/aix/probes/libcall_probe.c), and -DREMOVE_ALL_USE_DIRECTORY_ITERATOR,
 * because 6.1 lacks openat/fdopendir/unlinkat (libc++ provides the
 * directory iterator path for that). fchmodat/utimensat drop out by themselves, because 6.1
 * does not define AT_FDCWD and UTIME_OMIT.
 *
 * Include only for builds against the 6.1 sysroot (Open XL defines _AIX72
 * according to the build host, not the sysroot - no macro guard possible).
 */
#ifndef BTOP_AIX61_COMPAT_H
#define BTOP_AIX61_COMPAT_H

#if defined(_AIX)

#include <ctype.h>
#include <locale.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- locale objects: one global locale -------------------------------------- */
typedef struct __btop_locale *locale_t;

#define LC_GLOBAL_LOCALE ((locale_t)-1)
#define LC_COLLATE_MASK  (1 << LC_COLLATE)
#define LC_CTYPE_MASK    (1 << LC_CTYPE)
#define LC_MONETARY_MASK (1 << LC_MONETARY)
#define LC_NUMERIC_MASK  (1 << LC_NUMERIC)
#define LC_TIME_MASK     (1 << LC_TIME)
#define LC_MESSAGES_MASK (1 << LC_MESSAGES)
#define LC_ALL_MASK      (LC_COLLATE_MASK | LC_CTYPE_MASK | LC_MONETARY_MASK | \
                          LC_NUMERIC_MASK | LC_TIME_MASK | LC_MESSAGES_MASK)

static inline locale_t newlocale(int __mask, const char *__name, locale_t __base)
{
	static struct { int unused; } __btop_the_locale;
	(void)__mask; (void)__name; (void)__base;
	return (locale_t)&__btop_the_locale;
}
static inline locale_t duplocale(locale_t __loc) { return __loc; }
static inline void     freelocale(locale_t __loc) { (void)__loc; }
static inline locale_t uselocale(locale_t __loc) { (void)__loc; return LC_GLOBAL_LOCALE; }

#ifdef __cplusplus
}
#endif

/* strftime_l and noreturn declarations (uses the uselocale above) */
#include "aix71_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- *_l functions: the locale argument is ignored ------------------------- */
#define BTOP_CTYPE_L(name) \
	static inline int name##_l(int __c, locale_t __l) { (void)__l; return name(__c); }
BTOP_CTYPE_L(isalnum) BTOP_CTYPE_L(isalpha) BTOP_CTYPE_L(isblank) BTOP_CTYPE_L(iscntrl)
BTOP_CTYPE_L(isdigit) BTOP_CTYPE_L(isgraph) BTOP_CTYPE_L(islower) BTOP_CTYPE_L(isprint)
BTOP_CTYPE_L(ispunct) BTOP_CTYPE_L(isspace) BTOP_CTYPE_L(isupper) BTOP_CTYPE_L(isxdigit)
BTOP_CTYPE_L(tolower) BTOP_CTYPE_L(toupper)
#undef BTOP_CTYPE_L

#define BTOP_WCTYPE_L(name) \
	static inline int name##_l(wint_t __c, locale_t __l) { (void)__l; return name(__c); }
BTOP_WCTYPE_L(iswalnum) BTOP_WCTYPE_L(iswalpha) BTOP_WCTYPE_L(iswblank) BTOP_WCTYPE_L(iswcntrl)
BTOP_WCTYPE_L(iswdigit) BTOP_WCTYPE_L(iswgraph) BTOP_WCTYPE_L(iswlower) BTOP_WCTYPE_L(iswprint)
BTOP_WCTYPE_L(iswpunct) BTOP_WCTYPE_L(iswspace) BTOP_WCTYPE_L(iswupper) BTOP_WCTYPE_L(iswxdigit)
#undef BTOP_WCTYPE_L

static inline wint_t towlower_l(wint_t __c, locale_t __l) { (void)__l; return towlower(__c); }
static inline wint_t towupper_l(wint_t __c, locale_t __l) { (void)__l; return towupper(__c); }
static inline int    iswctype_l(wint_t __c, wctype_t __t, locale_t __l) { (void)__l; return iswctype(__c, __t); }
static inline wctype_t wctype_l(const char *__p, locale_t __l) { (void)__l; return wctype(__p); }

static inline int    strcoll_l(const char *__a, const char *__b, locale_t __l) { (void)__l; return strcoll(__a, __b); }
static inline size_t strxfrm_l(char *__d, const char *__s, size_t __n, locale_t __l) { (void)__l; return strxfrm(__d, __s, __n); }
static inline int    wcscoll_l(const wchar_t *__a, const wchar_t *__b, locale_t __l) { (void)__l; return wcscoll(__a, __b); }
static inline size_t wcsxfrm_l(wchar_t *__d, const wchar_t *__s, size_t __n, locale_t __l) { (void)__l; return wcsxfrm(__d, __s, __n); }

#ifdef __cplusplus
}
#endif

/* --- aligned_alloc (C11) ------------------------------------------------------ */
/* Missing in the libc before 6.1 TL9 (measured 2026-10-12 on 6100-07-10: the loader aborts with
 * "Symbol aligned_alloc ... not exported"). libc++ 19 calls it for over-aligned
 * operator new (__libcpp_aligned_alloc). Replaced through posix_memalign, which 6.1 has since TL0;
 * free(3) matches both. The macro affects libc++, libc++abi and btop (all via -include). */
#include <stdlib.h>
#include <errno.h>
static inline void *btop_aligned_alloc(size_t alignment, size_t size) {
	void *p = NULL;
	const int rc = posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size);
	if (rc != 0) { errno = rc; return NULL; }
	return p;
}
#define aligned_alloc btop_aligned_alloc

/* --- open(2) flag ----------------------------------------------------------- */
/* 6.1 has no O_CLOEXEC (7.1 and later). btop uses it only to open files for reading and starts
 * no child processes, so a neutral flag suffices. */
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#endif /* _AIX */
#endif /* BTOP_AIX61_COMPAT_H */
