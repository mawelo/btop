/* aix61_compat.h -- Projekt btop-aix (2026-10-12)
 *
 * Zweck: Wird beim Bau von libc++/libc++abi/libunwind (LLVM 19.1.7) und von btop
 * gegen das AIX-6.1-Sysroot per "-include" vorangestellt. Die libc von AIX 6.1
 * (bos.rte.libc 6.1.9.30, gemessen 2026-10-12 an 6100-09-04) kennt die
 * POSIX-2008-Locale-Schnittstelle nicht: kein locale_t, kein newlocale/uselocale/
 * freelocale, keine *_l-Funktionen (isdigit_l, strcoll_l, towupper_l, ...).
 * libc++ 19 setzt sie auf AIX voraus (locale_base_api/ibm.h).
 *
 * Nachbildung hier: ein einziges, globales Locale. newlocale liefert einen
 * Platzhalter (nie NULL, sonst wirft locale_byname), uselocale/freelocale tun
 * nichts, jede *_l-Funktion ruft die Fassung ohne Locale-Argument. Folge:
 * std::locale("de_DE") erzeugt kein eigenes Locale, Zahlen-/Zeitformate folgen
 * dem per setlocale(3) gesetzten Prozess-Locale. btop braucht nur das "C"-Locale.
 *
 * Dazu aus aix71_compat.h: strftime_l (ueber die hiesige uselocale-Attrappe) und
 * die noreturn-Deklarationen von abort/exit/_Exit.
 * Ausserdem aligned_alloc ueber posix_memalign (fehlt vor 6.1 TL9), damit das Paket ab 6.1 TL7 laedt.
 *
 * Weitere 6.1-Besonderheiten liegen in den Compiler-Flags (Preset aix61-openxl,
 * build_rt.pl --extra-flags): -fno-builtin-strlen -fno-builtin-strcmp, weil
 * Open XL sonst ___strlen64/___strcmp64 referenziert, die 6.1 nicht exportiert
 * (Sonde tools/aix/probes/libcall_probe.c), und -DREMOVE_ALL_USE_DIRECTORY_ITERATOR,
 * weil 6.1 openat/fdopendir/unlinkat nicht hat (libc++ hat dafuer den
 * Directory-Iterator-Pfad). fchmodat/utimensat entfallen von selbst, weil 6.1
 * AT_FDCWD und UTIME_OMIT nicht definiert.
 *
 * Nur fuer Builds gegen das 6.1-Sysroot einbinden (Open XL definiert _AIX72
 * nach dem Build-Host, nicht nach dem Sysroot - kein Makro-Schutz moeglich).
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

/* --- Locale-Objekte: ein globales Locale ----------------------------------- */
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

/* strftime_l und noreturn-Deklarationen (nutzt das uselocale von oben) */
#include "aix71_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- *_l-Funktionen: Locale-Argument wird ignoriert ------------------------ */
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
/* Fehlt in der libc vor 6.1 TL9 (gemessen 2026-10-12 an 6100-07-10: der Lader bricht mit
 * "Symbol aligned_alloc ... not exported" ab). libc++ 19 ruft es fuer ueberausgerichtetes
 * operator new (__libcpp_aligned_alloc). Ersatz ueber posix_memalign, das 6.1 seit TL0 hat;
 * free(3) passt zu beiden. Das Makro wirkt auf libc++, libc++abi und btop (alle per -include). */
#include <stdlib.h>
#include <errno.h>
static inline void *btop_aligned_alloc(size_t alignment, size_t size) {
	void *p = NULL;
	const int rc = posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size);
	if (rc != 0) { errno = rc; return NULL; }
	return p;
}
#define aligned_alloc btop_aligned_alloc

/* --- open(2)-Flag ----------------------------------------------------------- */
/* 6.1 kennt kein O_CLOEXEC (ab 7.1). btop oeffnet damit nur Lesedateien und startet
 * keine Kindprozesse, deshalb genuegt ein neutrales Flag. */
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#endif /* _AIX */
#endif /* BTOP_AIX61_COMPAT_H */
