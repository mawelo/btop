#!/usr/bin/perl
# make_sysroot.pl -- project btop-aix (2026-10-12)
#
# Who/what: On the AIX 7.2 build host, creates a sysroot of an older AIX level (7.1 or 6.1) from the tar files
#           packed on a 7.1 system and brings it into the state with which
#           libc++ 19 and btop are built against 7.1 (evidence: docs/aix-port.md, section 5):
#   1. Unpack the tar files (usr/include, usr/ccs/lib, a selection of usr/lib, libC.a).
#   2. Make absolute symlinks relative - otherwise e.g. usr/include/inttypes.h
#      or usr/lib/libc.a point to the 7.2 files of the build host.
#   3. Install the build host's (7.2) crti_64.o: the 7.1 crti requires _cdtors and
#      libC symbols that ld 7.2 does not provide; the 7.2 crti needs only atexit/exit.
#   4. math.h: put the C++ overloads behind __LIBC_NO_CPP_MATH_OVERLOADS__ as in 7.2
#      (libc++ defines abs/... itself, otherwise "missing exception specification").
#
# Usage (AIX 7.2 build host with Open XL):
#   perl make_sysroot.pl --root /build/btop-aix/sysroot71 --tag aix71 \
#        --tar /build/btop-aix/sysroot71.tar.gz --tar /build/btop-aix/sysroot71-libC.tar.gz
#   perl make_sysroot.pl --root /build/btop-aix/sysroot61 --tag aix61 --tar /build/btop-aix/sysroot61.tar.gz
#   (--tag: suffix of the backup copies crti_64.o.<tag>, math.h.orig.<tag>)
# Creating the tar files on the 7.1 host: see docs/aix-port.md (tar without -h).
use strict;
use warnings;
use Getopt::Long;
use File::Basename;
use File::Path qw(make_path);
use File::Spec;
use File::Copy qw(copy);

my ($root, @tars, $host_crti, $tag);
$host_crti = '/usr/ccs/lib/crti_64.o';
$tag = 'aix71';
GetOptions('root=s' => \$root, 'tar=s' => \@tars, 'host-crti=s' => \$host_crti, 'tag=s' => \$tag)
    or die "usage: $0 --root DIR --tar FILE [--tar FILE ...] [--host-crti FILE] [--tag aix71|aix61]\n";
die "--root missing\n" unless $root;
die "--tar missing\n" unless @tars;

make_path($root);
chdir $root or die "chdir $root: $!";

# 1. unpack
for my $t (@tars) {
    die "$t: not found\n" unless -f $t;
    my $cmd = ($t =~ /\.gz$/) ? "gzip -dc '$t' | tar -xf -" : "tar -xf '$t'";
    print "+ $cmd\n";
    system($cmd) == 0 or die "unpacking $t: rc=" . ($? >> 8) . "\n";
}

# 2. make symlinks relative (only targets that exist in the sysroot)
my ($rewritten, $outside) = (0, 0);
open my $find, '-|', 'find', '.', '-type', 'l' or die "find: $!";
while (my $l = <$find>) {
    chomp $l; $l =~ s{^\./}{};
    my $path = "$root/$l";
    my $target = readlink($path);
    next unless defined $target && $target =~ m{^/};
    my $in_root = "$root$target";
    if (-e $in_root || -l $in_root) {
        my $rel = File::Spec->abs2rel($in_root, dirname($path));
        unlink $path or die "unlink $path: $!";
        symlink($rel, $path) or die "symlink $path: $!";
        $rewritten++;
    } else {
        $outside++;
        print "  Note: $l -> $target points outside the sysroot (kept)\n";
    }
}
close $find;
print "Symlinks: $rewritten made relative, $outside point outside\n";

# 3. crti_64.o from the build host
my $crti = "$root/usr/ccs/lib/crti_64.o";
if (-f $crti && !-f "$crti.$tag") { copy($crti, "$crti.$tag") or die "copy: $!" }
copy($host_crti, $crti) or die "copy $host_crti: $!";
print "crti_64.o: 7.2 version installed, original kept as crti_64.o.$tag\n";

# 4. math.h
my $math = "$root/usr/include/math.h";
if (-f $math) {
    local $/;
    open my $in, '<', $math or die "$math: $!";
    my $src = <$in>; close $in;
    my $marker = '!defined(__LIBC_NO_CPP_MATH_OVERLOADS__)';
    if ($src !~ /\Q$marker\E/) {
        copy($math, "$math.orig.$tag") or die "copy: $!";
        my $n = ($src =~ s/^(#if\t!defined\(__COMPATMATH__\))[ \t]*$/$1 && $marker \/* btop-aix: as in AIX 7.2 *\//m);
        die "math.h: pattern not found\n" unless $n;
        open my $out, '>', $math or die "$math: $!";
        print $out $src; close $out;
        print "math.h: C++ overload guard added\n";
    } else {
        print "math.h: already patched\n";
    }
}
print "DONE: $root\n";
