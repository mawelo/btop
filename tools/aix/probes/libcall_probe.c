/* libcall_probe.c -- Projekt btop-aix (2026-10-12)
 *
 * Zweck: Zeigt, welche libc-Symbole IBM Open XL (LLVM 19) fuer strlen()/strcmp()
 * in 64-Bit-Objekten referenziert. Auf AIX 7.1/7.2 heissen sie ___strlen64 und
 * ___strcmp64; die libc von AIX 6.1 (6100-09) exportiert sie nicht (gemessen an
 * den Importen des 7.1-btop gegen die 6.1-Exportliste). Die Sonde prueft, ob
 * -fno-builtin-strlen -fno-builtin-strcmp die Umbenennung unterdrueckt.
 *
 * Bauen (AIX-7.2-Build-Host, Open XL, 64 Bit), Symbole mit nm:
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -c -o libcall_probe.o libcall_probe.c
 *   nm -X64 libcall_probe.o | grep -i 'str'
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -fno-builtin-strlen -fno-builtin-strcmp -c -o libcall_probe_nb.o libcall_probe.c
 *   nm -X64 libcall_probe_nb.o | grep -i 'str'
 */
#include <string.h>
#include <stddef.h>

size_t probe_len(const char *s) { return strlen(s); }
int probe_cmp(const char *a, const char *b) { return strcmp(a, b); }
