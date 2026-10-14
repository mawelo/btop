#!/usr/bin/perl
# check_imports.pl -- project btop-aix (2026-10-12)
# Who/what: Checks whether every symbol that an XCOFF binary (or a list of
#           imports) imports from system libraries is exported on a target system.
#           Serves as evidence for "built on AIX 7.2, runs on 7.1".
# Input:
#   --imports <file>    list of imports (one symbol per line), created with
#                       dump -X64 -Tv <binary> | awk '$4=="IMP"{print $NF}' | sort -u
#   --exports <file>    list of the target system's exports (one symbol per line), created with
#                       dump -X64 -Tv /usr/lib/libc.a ... | awk '$4=="EXP"{print $NF}' | sort -u
#   --ignore <regex>    symbols that do not come from libc/libpthreads/libperfstat
#                       (e.g. ^_Z.*|^__cxa|^_Unwind) and are therefore not checked.
# Output: missing symbols (one per line) and a summary line; rc 0 = nothing missing.
# Last known good usage:
#   perl tools/aix/check_imports.pl --imports <importlist>.txt \
#        --exports <exportlist>.txt (e.g. libc_exports_<level>.txt) \
#        --ignore '^(_Z|__cxa_|_Unwind_|__xlcxx|__dynamic_cast|__tls_get_mod)'
use strict;
use warnings;
use Getopt::Long;

my ($imports, $exports, $ignore) = ('', '', '');
GetOptions('imports=s' => \$imports, 'exports=s' => \$exports, 'ignore=s' => \$ignore)
    or die "usage: $0 --imports FILE --exports FILE [--ignore REGEX]\n";
die "--imports and --exports are required\n" unless $imports && $exports;

my %exp;
open my $e, '<', $exports or die "$exports: $!";
while (<$e>) { chomp; next unless length; $exp{(split)[-1]} = 1 }   # also lines "lib symbol" (the last field counts)
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
printf "checked=%d ignored=%d missing=%d exports=%d\n", $total, $skipped, scalar @missing, scalar keys %exp;
exit(@missing ? 1 : 0);
