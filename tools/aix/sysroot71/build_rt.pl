#!/usr/bin/perl
# build_rt.pl -- Projekt btop-aix (2026-10-12)
#
# Zweck: Baut libunwind, libc++abi und libc++ aus den LLVM-19.1.7-Quellen mit
# IBM Open XL 17.1.3 auf einem AIX-7.2-Host GEGEN EIN AELTERES AIX-SYSROOT (7.1 oder 6.1), statisch,
# und installiert sie nach <prefix>. Damit kann btop fuer AIX 7.1 gelinkt
# werden (die IBM-Laufzeit 17.1.3 laeuft auf 7.1 nicht: PTHREAD_MUTEX_INITIALIZER
# hat dort ein anderes Layout, strftime_l fehlt).
#
# Laeuft auf dem AIX-Build-Host (AIX-7.2-Build-Host mit Open XL). Aufruf:
#   perl build_rt.pl --src /build/btop-aix/llvm-project-19.1.7.src \
#        --sysroot /build/btop-aix/sysroot71 --prefix /build/btop-aix/rt71 \
#        --build /build/btop-aix/build-rt71 --compat /build/btop-aix/aix71_compat.h [--jobs 12] [--configure-only]
#        [--extra-flags "-fno-builtin-strlen -fno-builtin-strcmp -DREMOVE_ALL_USE_DIRECTORY_ITERATOR"]  (AIX 6.1, s. aix61_compat.h)
# Compiler: ausschliesslich IBM Open XL, absoluter Pfad (Regel aix.md).
use strict;
use warnings;
use Getopt::Long;
use File::Path qw(make_path);

my %o = (jobs => 12, 'extra-flags' => '');
GetOptions(\%o, 'src=s', 'sysroot=s', 'prefix=s', 'build=s', 'compat=s', 'jobs=i', 'configure-only', 'extra-flags=s')
    or die "Aufruf: $0 --src DIR --sysroot DIR --prefix DIR --build DIR --compat DATEI [--jobs N] [--configure-only] [--extra-flags FLAGS]\n";
for (qw(src sysroot prefix build compat)) { die "--$_ fehlt\n" unless $o{$_} }
for (qw(src sysroot compat)) { die "$o{$_}: nicht vorhanden\n" unless -e $o{$_} }

$ENV{OBJECT_MODE} = '64';
$ENV{PATH} = "/opt/freeware/bin:$ENV{PATH}";
my $cc  = '/opt/IBM/openxlC/17.1.3/bin/ibm-clang_r';
my $cxx = '/opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r';
die "$cc fehlt\n" unless -x $cc;

my $flags = "-m64 --sysroot=$o{sysroot} -include $o{compat} -D__LIBC_NO_CPP_MATH_OVERLOADS__ $o{'extra-flags'}";
# C++: Wrapper-Header mit extern "C" um die 7.1-C-Header (make_cxxwrap.pl)
my $cxxflags = "$flags -isystem $o{sysroot}/cxxwrap";
die "$o{sysroot}/cxxwrap fehlt - erst make_cxxwrap.pl laufen lassen\n" unless -d "$o{sysroot}/cxxwrap";
# Bibliotheken zuerst aus dem Sysroot (der Treiber gibt kein -L fuer das Sysroot mit).
my $ldflags = "-L$o{sysroot}/usr/lib";

make_path($o{build});
chdir $o{build} or die "chdir $o{build}: $!";

my @cmake = (
    'cmake', '-G', 'Ninja', "$o{src}/runtimes",
    '-C', "$o{src}/libcxx/cmake/caches/AIX.cmake",
    "-DCMAKE_C_COMPILER=$cc", "-DCMAKE_CXX_COMPILER=$cxx",
    "-DCMAKE_C_FLAGS=$flags", "-DCMAKE_CXX_FLAGS=$cxxflags",
    "-DCMAKE_EXE_LINKER_FLAGS=$ldflags", "-DCMAKE_SHARED_LINKER_FLAGS=$ldflags -Wl,-G -Wl,-bcdtors:all:-2147483548:s",
    "-DCMAKE_INSTALL_PREFIX=$o{prefix}",
    '-DCMAKE_BUILD_TYPE=Release',
    '-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx',
    '-DLIBCXX_ENABLE_SHARED=OFF', '-DLIBCXX_ENABLE_STATIC=ON',
    '-DLIBCXXABI_ENABLE_SHARED=OFF', '-DLIBCXXABI_ENABLE_STATIC=ON',
    '-DLIBUNWIND_ENABLE_SHARED=OFF', '-DLIBUNWIND_ENABLE_STATIC=ON',
    '-DLIBCXX_INCLUDE_TESTS=OFF', '-DLIBCXX_INCLUDE_BENCHMARKS=OFF',
    '-DLIBCXXABI_INCLUDE_TESTS=OFF', '-DLIBUNWIND_INCLUDE_TESTS=OFF',
    '-DLIBCXX_CXX_ABI=libcxxabi', '-DLIBCXXABI_USE_LLVM_UNWINDER=ON',
    '-DLIBCXX_USE_COMPILER_RT=ON', '-DLIBCXXABI_USE_COMPILER_RT=ON', '-DLIBUNWIND_USE_COMPILER_RT=ON',
    '-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON',
    '-DLIBCXX_HARDENING_MODE=none',
    # __cxa_thread_atexit_impl gibt es erst in der 7.2-libc; der CMake-Test findet es ueber
    # die Host-Bibliotheken und wuerde ein Binary erzeugen, das auf 7.1 nicht laedt.
    '-DLIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF',
);
print "+ @cmake\n";
system(@cmake) == 0 or die "cmake configure rc=" . ($? >> 8) . "\n";
exit 0 if $o{'configure-only'};

my @build = ('ninja', '-j', $o{jobs}, 'cxx', 'cxxabi', 'unwind');
print "+ @build\n";
system(@build) == 0 or die "ninja rc=" . ($? >> 8) . "\n";

my @install = ('ninja', 'install-cxx', 'install-cxxabi', 'install-unwind');
print "+ @install\n";
system(@install) == 0 or die "ninja install rc=" . ($? >> 8) . "\n";
print "FERTIG: $o{prefix}\n";
