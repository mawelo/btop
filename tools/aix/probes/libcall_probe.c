/* libcall_probe.c -- project btop-aix (2026-10-12)
 *
 * Who/what: Shows which libc symbols IBM Open XL (LLVM 19) references for strlen()/strcmp()
 * in 64-bit objects. On AIX 7.1/7.2 they are named ___strlen64 and
 * ___strcmp64; the AIX 6.1 libc (6100-09) does not export them (measured on
 * the imports of the 7.1 btop against the 6.1 export list). The probe checks whether
 * -fno-builtin-strlen -fno-builtin-strcmp suppresses the renaming.
 *
 * Build (AIX 7.2 build host, Open XL, 64 bit), symbols with nm:
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -c -o libcall_probe.o libcall_probe.c
 *   nm -X64 libcall_probe.o | grep -i 'str'
 *   /opt/IBM/openxlC/17.1.3/bin/ibm-clang_r -m64 -O2 -fno-builtin-strlen -fno-builtin-strcmp -c -o libcall_probe_nb.o libcall_probe.c
 *   nm -X64 libcall_probe_nb.o | grep -i 'str'
 */
#include <string.h>
#include <stddef.h>

size_t probe_len(const char *s) { return strlen(s); }
int probe_cmp(const char *a, const char *b) { return strcmp(a, b); }
