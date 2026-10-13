#!/usr/bin/perl
# make_cxxwrap.pl -- Projekt btop-aix (2026-10-12)
#
# Zweck: Die C-Header von AIX 7.1 tragen keine extern-"C"-Klammern (die kamen
# erst mit 7.2; auf 7.1 erledigten das die IBM-Compiler intern). Mit Open XL
# gegen das 7.1-Sysroot bekommen isdigit(), catopen(), _Errno() usw. deshalb
# C++-Bindung, und der Link scheitert mit "_Z7isdigiti undefined".
# Dieses Skript erzeugt unter <sysroot>/cxxwrap fuer jeden Header einen
# Wrapper (auch fuer Header mit teilweiser Klammer, z. B. sys/socket.h):
#     extern "C" { #include_next <name> }
# Der Baum wird nur fuer C++ per "-isystem <sysroot>/cxxwrap" eingebunden,
# nach den libc++-Headern und vor dem Sysroot.
#
# Aufruf (AIX-7.2-Build-Host mit Open XL): perl make_cxxwrap.pl --sysroot /build/btop-aix/sysroot71
use strict;
use warnings;
use Getopt::Long;
use File::Find;
use File::Path qw(make_path);
use File::Basename;

my $sysroot;
GetOptions('sysroot=s' => \$sysroot) or die "Aufruf: $0 --sysroot DIR\n";
die "--sysroot fehlt\n" unless $sysroot && -d "$sysroot/usr/include";
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
    # Auch Header mit teilweiser extern-"C"-Klammer wrappen (sys/socket.h klammert socket() nicht):
    # eine zusaetzliche aeussere extern-"C"-Klammer ist harmlos.
    $skipped++ if $src =~ /extern\s+"C"/;
    my $dst = "$out/$rel";
    make_path(dirname($dst));
    open my $o, '>', $dst or die "$dst: $!";
    print $o "/* btop-aix cxxwrap: AIX-7.1-Header ohne extern \"C\" (make_cxxwrap.pl) */\n",
             "#ifdef __cplusplus\nextern \"C\" {\n#endif\n",
             "#include_next <$rel>\n",
             "#ifdef __cplusplus\n}\n#endif\n";
    close $o;
    $wrapped++;
}}, $inc);
print "cxxwrap: $wrapped Header gewrappt ($skipped davon hatten schon teilweise extern \"C\") -> $out\n";
