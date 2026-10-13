#!/usr/bin/perl
# make_sysroot.pl -- Projekt btop-aix (2026-10-12)
#
# Zweck: Legt auf dem AIX-7.2-Build-Host ein Sysroot einer aelteren AIX-Version (7.1 oder 6.1) aus den auf einem
# 7.1-System gepackten Tar-Dateien an und bringt es in den Zustand, mit dem
# libc++ 19 und btop gegen 7.1 gebaut werden (Belege: docs/aix-port.md, Abs. 5):
#   1. Tar-Dateien entpacken (usr/include, usr/ccs/lib, Auswahl usr/lib, libC.a).
#   2. Absolute Symlinks relativ machen - sonst zeigen z. B. usr/include/inttypes.h
#      oder usr/lib/libc.a auf die 7.2-Dateien des Build-Hosts.
#   3. crti_64.o des Build-Hosts (7.2) einsetzen: das 7.1-crti verlangt _cdtors und
#      libC-Symbole, die ld 7.2 nicht liefert; das 7.2-crti braucht nur atexit/exit.
#   4. math.h: C++-Overloads wie in 7.2 hinter __LIBC_NO_CPP_MATH_OVERLOADS__ legen
#      (libc++ definiert abs/... selbst, sonst "missing exception specification").
#
# Aufruf (AIX-7.2-Build-Host mit Open XL):
#   perl make_sysroot.pl --root /build/btop-aix/sysroot71 --tag aix71 \
#        --tar /build/btop-aix/sysroot71.tar.gz --tar /build/btop-aix/sysroot71-libC.tar.gz
#   perl make_sysroot.pl --root /build/btop-aix/sysroot61 --tag aix61 --tar /build/btop-aix/sysroot61.tar.gz
#   (--tag: Suffix der Sicherungskopien crti_64.o.<tag>, math.h.orig.<tag>)
# Erzeugung der Tar-Dateien auf dem 7.1-Host: siehe docs/aix-port.md (tar ohne -h).
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
    or die "Aufruf: $0 --root DIR --tar DATEI [--tar DATEI ...] [--host-crti DATEI] [--tag aix71|aix61]\n";
die "--root fehlt\n" unless $root;
die "--tar fehlt\n" unless @tars;

make_path($root);
chdir $root or die "chdir $root: $!";

# 1. entpacken
for my $t (@tars) {
    die "$t: nicht vorhanden\n" unless -f $t;
    my $cmd = ($t =~ /\.gz$/) ? "gzip -dc '$t' | tar -xf -" : "tar -xf '$t'";
    print "+ $cmd\n";
    system($cmd) == 0 or die "entpacken von $t: rc=" . ($? >> 8) . "\n";
}

# 2. Symlinks relativ machen (nur Ziele, die im Sysroot existieren)
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
        print "  Hinweis: $l -> $target zeigt aus dem Sysroot heraus (bleibt)\n";
    }
}
close $find;
print "Symlinks: $rewritten relativ gemacht, $outside zeigen nach aussen\n";

# 3. crti_64.o vom Build-Host
my $crti = "$root/usr/ccs/lib/crti_64.o";
if (-f $crti && !-f "$crti.$tag") { copy($crti, "$crti.$tag") or die "copy: $!" }
copy($host_crti, $crti) or die "copy $host_crti: $!";
print "crti_64.o: 7.2-Fassung eingesetzt, Original als crti_64.o.$tag\n";

# 4. math.h
my $math = "$root/usr/include/math.h";
if (-f $math) {
    local $/;
    open my $in, '<', $math or die "$math: $!";
    my $src = <$in>; close $in;
    my $marker = '!defined(__LIBC_NO_CPP_MATH_OVERLOADS__)';
    if ($src !~ /\Q$marker\E/) {
        copy($math, "$math.orig.$tag") or die "copy: $!";
        my $n = ($src =~ s/^(#if\t!defined\(__COMPATMATH__\))[ \t]*$/$1 && $marker \/* btop-aix: wie AIX 7.2 *\//m);
        die "math.h: Muster nicht gefunden\n" unless $n;
        open my $out, '>', $math or die "$math: $!";
        print $out $src; close $out;
        print "math.h: C++-Overload-Schutz ergaenzt\n";
    } else {
        print "math.h: bereits gepatcht\n";
    }
}
print "FERTIG: $root\n";
