#!/usr/bin/perl
# make_cxxwrap.pl -- project btop-aix (2026-10-12)
#
# Who/what: The AIX 7.1 C headers carry no extern "C" brackets (those came
#           only with 7.2; on 7.1 the IBM compilers handled this internally). With Open XL
#           against the 7.1 sysroot, isdigit(), catopen(), _Errno() etc. therefore get
#           C++ linkage, and the link fails with "_Z7isdigiti undefined".
#           This script creates under <sysroot>/cxxwrap one wrapper per header
#           (also for headers with partial brackets, e.g. sys/socket.h):
#     extern "C" { #include_next <name> }
#           The tree is included for C++ only, via "-isystem <sysroot>/cxxwrap",
#           after the libc++ headers and before the sysroot.
#
# Usage (AIX 7.2 build host with Open XL): perl make_cxxwrap.pl --sysroot /build/btop-aix/sysroot71
use strict;
use warnings;
use Getopt::Long;
use File::Find;
use File::Path qw(make_path);
use File::Basename;

my $sysroot;
GetOptions('sysroot=s' => \$sysroot) or die "usage: $0 --sysroot DIR\n";
die "--sysroot missing\n" unless $sysroot && -d "$sysroot/usr/include";
my $inc = "$sysroot/usr/include";
my $out = "$sysroot/cxxwrap";
my ($wrapped, $skipped) = (0, 0);

find({ no_chdir => 1, follow => 0, wanted => sub {
    return unless -f $_ && /\.h$/;
    my $rel = $File::Find::name;
    $rel =~ s{^\Q$inc\E/}{};
    local $/;
    open my $fh, '<', $_ or do { warn "$_: $!\n"; return };
    my $src = <$fh>; close $fh;
    # Also wrap headers with a partial extern "C" bracket (sys/socket.h does not bracket socket()):
    # an additional outer extern "C" bracket is harmless.
    $skipped++ if $src =~ /extern\s+"C"/;
    my $dst = "$out/$rel";
    make_path(dirname($dst));
    open my $o, '>', $dst or die "$dst: $!";
    print $o "/* btop-aix cxxwrap: AIX 7.1 header without extern \"C\" (make_cxxwrap.pl) */\n",
             "#ifdef __cplusplus\nextern \"C\" {\n#endif\n",
             "#include_next <$rel>\n",
             "#ifdef __cplusplus\n}\n#endif\n";
    close $o;
    $wrapped++;
}}, $inc);
print "cxxwrap: $wrapped headers wrapped ($skipped of them already had partial extern \"C\") -> $out\n";
