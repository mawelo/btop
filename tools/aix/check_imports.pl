#!/usr/bin/perl
# check_imports.pl -- Projekt btop-aix (2026-10-12)
# Zweck: Prueft, ob alle Symbole, die ein XCOFF-Binary (oder eine Liste von
#        Importen) aus Systembibliotheken importiert, auf einem Zielsystem
#        exportiert werden. Dient dem Nachweis "auf AIX 7.2 gebaut, laeuft auf 7.1".
# Eingaben:
#   --imports <datei>   Liste der Importe (ein Symbol je Zeile), erzeugt mit
#                       dump -X64 -Tv <binary> | awk '$4=="IMP"{print $NF}' | sort -u
#   --exports <datei>   Liste der Exporte des Zielsystems (ein Symbol je Zeile), erzeugt mit
#                       dump -X64 -Tv /usr/lib/libc.a ... | awk '$4=="EXP"{print $NF}' | sort -u
#   --ignore <regex>    Symbole, die nicht aus libc/libpthreads/libperfstat kommen
#                       (z. B. ^_Z.*|^__cxa|^_Unwind) und daher nicht geprueft werden.
# Ausgabe: fehlende Symbole (eines je Zeile) und eine Summenzeile; rc 0 = nichts fehlt.
# Letzter bekannter guter Aufruf:
#   perl tools/aix/check_imports.pl --imports <importliste>.txt \
#        --exports <exportliste>.txt (z. B. tools/aix/analysis/libc_exports_*.txt) \
#        --ignore '^(_Z|__cxa_|_Unwind_|__xlcxx|__dynamic_cast|__tls_get_mod)'
use strict;
use warnings;
use Getopt::Long;

my ($imports, $exports, $ignore) = ('', '', '');
GetOptions('imports=s' => \$imports, 'exports=s' => \$exports, 'ignore=s' => \$ignore)
    or die "Aufruf: $0 --imports DATEI --exports DATEI [--ignore REGEX]\n";
die "--imports und --exports sind Pflicht\n" unless $imports && $exports;

my %exp;
open my $e, '<', $exports or die "$exports: $!";
while (<$e>) { chomp; next unless length; $exp{(split)[-1]} = 1 }   # auch Zeilen "lib symbol" (letztes Feld zaehlt)
close $e;

my ($total, $skipped, @missing) = (0, 0);
open my $i, '<', $imports or die "$imports: $!";
while (<$i>) {
    chomp; next unless length;
    if ($ignore ne '' && /$ignore/) { $skipped++; next }
    $total++;
    push @missing, $_ unless $exp{$_};
}
close $i;

print "$_\n" for @missing;
printf "geprueft=%d ignoriert=%d fehlend=%d exporte=%d\n", $total, $skipped, scalar @missing, scalar keys %exp;
exit(@missing ? 1 : 0);
