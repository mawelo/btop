/* host-side preamble: AIX basic types for LP64 layout analysis of libperfstat headers (not an AIX build) */
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <sys/types.h>
typedef unsigned long long u_longlong_t;
typedef long long longlong_t;
typedef unsigned int uint;
typedef unsigned short cid_t;
typedef int rsethandle_t;
typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef unsigned long long uint64;
typedef long long int64;
typedef unsigned int uint32;
typedef int int32;
#include <netinet/in.h>
#include <sys/socket.h>
typedef u_longlong_t psize_t;
typedef long tid_t;
typedef u_longlong_t dev64_t;
#ifndef MAXCORRALNAMELEN
#define MAXCORRALNAMELEN 25
#endif
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
typedef u_longlong_t rpn64_t;
typedef unsigned int uint_t;
typedef u_longlong_t cnt64_t;
typedef u_longlong_t size64_t;
typedef long long ssize64_t;
typedef int pid32_t;
typedef long long time64_t;
#define NSIG64 256
typedef struct { unsigned long long ss[4]; } sigset64_t;
struct trusage64 { long long r[20]; };
struct rlimit64 { long long a, b; };
