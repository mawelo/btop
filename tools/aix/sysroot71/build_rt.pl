#!/usr/bin/perl
# build_rt.pl -- project btop-aix (2026-10-12)
#
# Who/what: Builds libunwind, libc++abi and libc++ from the LLVM 19.1.7 sources with
#           IBM Open XL 17.1.3 on an AIX 7.2 host AGAINST AN OLDER AIX SYSROOT (7.1 or 6.1), statically,
#           and installs them into <prefix>. This lets btop be linked for AIX 7.1
#           (the IBM runtime 17.1.3 does not run on 7.1: PTHREAD_MUTEX_INITIALIZER
#           has a different layout there, strftime_l is missing).
#
# Runs on the AIX build host (AIX 7.2 build host with Open XL). Usage:
#   perl build_rt.pl --src /build/btop-aix/llvm-project-19.1.7.src \
#        --sysroot /build/btop-aix/sysroot71 --prefix /build/btop-aix/rt71 \
#        --build /build/btop-aix/build-rt71 --compat /build/btop-aix/aix71_compat.h [--jobs 12] [--configure-only]
#        [--extra-flags "-fno-builtin-strlen -fno-builtin-strcmp -DREMOVE_ALL_USE_DIRECTORY_ITERATOR"]  (AIX 6.1, see aix61_compat.h)
# Compiler: IBM Open XL only, absolute path (rule aix.md).
use strict;
use warnings;
use Getopt::Long;
use File::Path qw(make_path);

my %o = (jobs => 12, 'extra-flags' => '');
GetOptions(\%o, 'src=s', 'sysroot=s', 'prefix=s', 'build=s', 'compat=s', 'jobs=i', 'configure-only', 'extra-flags=s')
    or die "usage: $0 --src DIR --sysroot DIR --prefix DIR --build DIR --compat FILE [--jobs N] [--configure-only] [--extra-flags FLAGS]\n";
for (qw(src sysroot prefix build compat)) { die "--$_ missing\n" unless $o{$_} }
for (qw(src sysroot compat)) { die "$o{$_}: not found\n" unless -e $o{$_} }

$ENV{OBJECT_MODE} = '64';
$ENV{PATH} = "/opt/freeware/bin:$ENV{PATH}";
my $cc  = '/opt/IBM/openxlC/17.1.3/bin/ibm-clang_r';
my $cxx = '/opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r';
die "$cc missing\n" unless -x $cc;

my $flags = "-m64 --sysroot=$o{sysroot} -include $o{compat} -D__LIBC_NO_CPP_MATH_OVERLOADS__ $o{'extra-flags'}";
# C++: wrapper headers with extern "C" around the 7.1 C headers (make_cxxwrap.pl)
my $cxxflags = "$flags -isystem $o{sysroot}/cxxwrap";
die "$o{sysroot}/cxxwrap missing - run make_cxxwrap.pl first\n" unless -d "$o{sysroot}/cxxwrap";
# Libraries from the sysroot first (the driver passes no -L for the sysroot).
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
    # __cxa_thread_atexit_impl exists only from the 7.2 libc on; the CMake test finds it through
    # the host libraries and would produce a binary that does not load on 7.1.
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
print "DONE: $o{prefix}\n";
