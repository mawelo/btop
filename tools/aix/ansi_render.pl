#!/usr/bin/perl
# ansi_render.pl -- project btop-aix (2026-10-12)
# Who/what: Renders a recorded terminal output stream (btop screen with
#           ANSI cursor positioning) into a flat text image, so that the last
#           screen content of a non-interactive run can be checked.
#           Understood: CSI H/f (set cursor), A/B/C/D (move), J/K (erase),
#           s/u and ESC 7/8 (save cursor); SGR and private modes are ignored;
#           wide characters (UTF-8) occupy one cell (approximation).
# Usage: perl tools/aix/ansi_render.pl --rows 50 --cols 160 <file> > image.txt
# Last known good usage:
#   perl tools/aix/ansi_render.pl --rows 50 --cols 160 --keep-last-frame <run>_screen.raw
use strict;
use warnings;
use utf8;
use Getopt::Long;
binmode STDOUT, ':encoding(UTF-8)';

my ($rows, $cols, $keep_last) = (50, 160, 0);
GetOptions('rows=i' => \$rows, 'cols=i' => \$cols, 'keep-last-frame' => \$keep_last)
    or die "usage: $0 [--rows N] [--cols N] [--keep-last-frame] FILE\n";
my $file = shift or die "file missing\n";
open my $fh, '<:raw', $file or die "$file: $!";
local $/; my $data = <$fh>; close $fh;
utf8::decode($data);

# --keep-last-frame: btop clears the screen on exit (ESC [2J); to check the
# last image, the stream is cut before the last clear.
if ($keep_last) {
    my $pos = rindex($data, "\e[2J");
    $data = substr($data, 0, $pos) if $pos > 0;
}

my @screen = map { [ (' ') x $cols ] } 1 .. $rows;
my ($r, $c) = (0, 0);
my ($sr, $sc) = (0, 0);

sub clamp { my ($v, $lo, $hi) = @_; $v < $lo ? $lo : $v > $hi ? $hi : $v }

my $i = 0;
my $len = length $data;
while ($i < $len) {
    my $ch = substr($data, $i, 1);
    if ($ch eq "\e") {
        my $next = substr($data, $i + 1, 1);
        if ($next eq '[') {
            if (substr($data, $i + 2) =~ /^([\?>!]?[0-9;]*)([\x40-\x7e])/) {
                my ($params, $final) = ($1, $2);
                $i += 2 + length($params) + 1;
                my @p = split /;/, ($params =~ s/^[\?>!]//r);
                if ($final eq 'H' or $final eq 'f') {
                    $r = clamp((($p[0] // 1) || 1) - 1, 0, $rows - 1);
                    $c = clamp((($p[1] // 1) || 1) - 1, 0, $cols - 1);
                } elsif ($final eq 'A') { $r = clamp($r - (($p[0] // 1) || 1), 0, $rows - 1) }
                elsif ($final eq 'B') { $r = clamp($r + (($p[0] // 1) || 1), 0, $rows - 1) }
                elsif ($final eq 'C') { $c = clamp($c + (($p[0] // 1) || 1), 0, $cols - 1) }
                elsif ($final eq 'D') { $c = clamp($c - (($p[0] // 1) || 1), 0, $cols - 1) }
                elsif ($final eq 'G') { $c = clamp((($p[0] // 1) || 1) - 1, 0, $cols - 1) }
                elsif ($final eq 'd') { $r = clamp((($p[0] // 1) || 1) - 1, 0, $rows - 1) }
                elsif ($final eq 'J') {
                    my $mode = $p[0] // 0;
                    if ($mode == 2 or $mode == 3) { @screen = map { [ (' ') x $cols ] } 1 .. $rows }
                    elsif ($mode == 0) { $screen[$r][$_] = ' ' for $c .. $cols - 1; $screen[$_] = [ (' ') x $cols ] for $r + 1 .. $rows - 1 }
                    else { $screen[$r][$_] = ' ' for 0 .. $c; $screen[$_] = [ (' ') x $cols ] for 0 .. $r - 1 }
                } elsif ($final eq 'K') {
                    my $mode = $p[0] // 0;
                    if ($mode == 0) { $screen[$r][$_] = ' ' for $c .. $cols - 1 }
                    elsif ($mode == 1) { $screen[$r][$_] = ' ' for 0 .. $c }
                    else { $screen[$r] = [ (' ') x $cols ] }
                } elsif ($final eq 's') { ($sr, $sc) = ($r, $c) }
                elsif ($final eq 'u') { ($r, $c) = ($sr, $sc) }
                # SGR (m), private modes (h/l), others: ignored
                next;
            }
            $i += 2; next;
        } elsif ($next eq '7') { ($sr, $sc) = ($r, $c); $i += 2; next }
        elsif ($next eq '8') { ($r, $c) = ($sr, $sc); $i += 2; next }
        elsif ($next eq ']') {   # OSC ... BEL or ST
            my $rest = substr($data, $i);
            if ($rest =~ /^\e\].*?(\a|\e\\)/s) { $i += length($&); next }
            $i += 2; next;
        } elsif ($next eq '(' or $next eq ')') { $i += 3; next }
        else { $i += 2; next }
    } elsif ($ch eq "\r") { $c = 0; $i++; next }
    elsif ($ch eq "\n") { $r = clamp($r + 1, 0, $rows - 1); $c = 0; $i++; next }
    elsif ($ch eq "\b") { $c = clamp($c - 1, 0, $cols - 1); $i++; next }
    elsif ($ch eq "\a" or $ch eq "\x0f" or $ch eq "\x0e") { $i++; next }
    elsif (ord($ch) < 0x20) { $i++; next }
    $screen[$r][$c] = $ch;
    $c++;
    if ($c >= $cols) { $c = $cols - 1 }
    $i++;
}

for my $line (@screen) {
    my $t = join '', @$line;
    $t =~ s/\s+$//;
    print "$t\n";
}
