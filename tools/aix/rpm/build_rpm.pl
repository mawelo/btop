#!/usr/bin/perl
# build_rpm.pl -- Projekt btop-aix (2026-10-12)
#
# Wer/Was : Baut auf dem AIX-Build-Host das Binaer-RPM und ein gleichwertiges Tar-Archiv fuer
#           btop aus einem fertig gebauten Binary und dem Quellbaum (Themes, README, LICENSE).
#           Legt die Dateibaum-Vorlage (/opt/btop, /usr/bin/btop) EINMAL an; Spec und Tar
#           uebernehmen sie unveraendert (eine Quelle fuer das Layout).
# Wann    : 2026-10-12.
# Warum   : Ein installierbares Paket fuer alle AIX-Staende ab 6.1 TL7 (Entscheidung 2026-10-12: nur der
#           6.1-Bau wird ausgeliefert, er laeuft bis 7.3); nachvollziehbar aus Commit,
#           Binary und Spec (tools/aix/rpm/btop.spec, Starter tools/aix/rpm/btop.sh).
# Aufruf  : perl tools/aix/rpm/build_rpm.pl --binary build/aix61-openxl/btop --source . \
#               --commit <id> --out <dir> [--release 1] [--target ppc-ibm-aix6.1] [--topdir DIR]
#           --target setzt das rpm-OS-Feld und den Dateinamen (btop-<ver>-<rel>.aix6.1.ppc.rpm): aix6.1 = Mindest-
#           stand; rpm auf 7.1/7.2/7.3 nimmt es ueber seine Kompatibilitaetstabelle an (gemessen 2026-10-12),
#           ein nacktes "aix" lehnt es ab ("intended for a different operating system").
#           Laeuft auf AIX mit rpm.rte 4.x (rpmbuild) und dem System-Perl; keine CPAN-Module.
# Ausgabe : <out>/btop-<ver>-<rel>.<os>.ppc.rpm, <out>/btop-<ver>-<rel>.<os>.ppc.tar.gz,
#           <out>/rpm-info.txt (rpm -qpi/-qpl/--requires; keine Pruefsumme, siehe unten). rc 0 nur bei vollstaendigem Lauf.
use strict;
use warnings;
use Getopt::Long;
use File::Path qw(make_path remove_tree);
use File::Copy qw(copy);
use File::Basename qw(basename);
use Cwd qw(abs_path getcwd);

my %o = (release => '1', target => 'ppc-ibm-aix6.1', spec => '');
GetOptions(\%o, 'binary=s', 'source=s', 'commit=s', 'out=s', 'release=s', 'target=s', 'topdir=s', 'spec=s')
    or die "Aufruf: $0 --binary DATEI --source DIR --commit ID --out DIR [--release N] [--target TRIPLE] [--topdir DIR]\n";
for (qw(binary source commit out)) { die "--$_ fehlt\n" unless defined $o{$_} && length $o{$_} }
die "$o{binary}: kein ausfuehrbares Binary\n" unless -f $o{binary} && -x _;
die "$o{source}: kein btop-Quellbaum (themes/ fehlt)\n" unless -d "$o{source}/themes";

my $src    = abs_path($o{source});
my $binary = abs_path($o{binary});
make_path($o{out});
my $out    = abs_path($o{out});
my $topdir = $o{topdir} ? $o{topdir} : "$out/rpmbuild";
my $spec   = $o{spec} || "$src/tools/aix/rpm/btop.spec";
my $launch = "$src/tools/aix/rpm/btop.sh";
my $readme = "$src/tools/aix/rpm/README.AIX";
for ($spec, $launch, $readme, "$src/README.md", "$src/LICENSE") { die "$_: fehlt\n" unless -f $_ }

# Version aus dem Quellbaum (Global::Version in src/btop.cpp), nicht aus dem Kopf
my $version = '';
open my $vf, '<', "$src/src/btop.cpp" or die "src/btop.cpp: $!";
while (<$vf>) { if (/const\s+string\s+Version\s*=\s*"([^"]+)"/) { $version = $1; last } }
close $vf;
die "Version nicht in src/btop.cpp gefunden\n" unless $version;
(my $target_os = $o{target}) =~ s/^.*-(aix[0-9.]*).*$/$1/;

sub run { print "+ @_\n"; system(@_) == 0 or die "FAILED (rc=" . ($? >> 8) . "): @_\n" }

# 1. Dateibaum-Vorlage (die eine Quelle fuer RPM und Tar)
my $tree = "$topdir/tree";
remove_tree($tree) if -d $tree;
make_path("$tree/opt/btop/bin", "$tree/opt/btop/share/btop", "$tree/opt/btop/share/doc/btop", "$tree/usr/bin",
          map { "$topdir/$_" } qw(BUILD RPMS SOURCES SPECS SRPMS BUILDROOT));
copy($binary, "$tree/opt/btop/bin/btop.bin") or die "copy binary: $!";
copy($launch, "$tree/opt/btop/bin/btop")     or die "copy launcher: $!";
chmod 0755, "$tree/opt/btop/bin/btop.bin", "$tree/opt/btop/bin/btop";
run('cp', '-Rp', "$src/themes", "$tree/opt/btop/share/btop/themes");
for my $d ("$src/README.md", "$src/LICENSE", $readme) { copy($d, "$tree/opt/btop/share/doc/btop/" . basename($d)) or die "copy $d: $!" }
symlink('/opt/btop/bin/btop', "$tree/usr/bin/btop") or die "symlink: $!";
# Rechte unabhaengig von der umask des Build-Hosts (Re-Review 2026-10-14, R1: Release 2 hatte Themes 0664, Verzeichnis 0775,
# weil cp -Rp die Rechte des Checkouts uebernahm). Gilt fuer RPM (zusaetzlich %defattr im Spec) und Tar.
run('chmod', '-R', 'u=rwX,go=rX', "$tree/opt");
printf "Baum: %s (Version %s, Release %s, Commit %s, Ziel %s)\n", $tree, $version, $o{release}, $o{commit}, $o{target};

# 2. RPM
run('rpmbuild', '-bb', '--target', $o{target},
    '--define', "_topdir $topdir", '--define', "btop_tree $tree",
    '--define', "btop_version $version", '--define', "btop_release $o{release}",
    '--define', "btop_commit $o{commit}", '--define', "btop_target $target_os", $spec);
my @rpms = glob("$topdir/RPMS/*/btop-$version-$o{release}*.rpm");
die "kein RPM unter $topdir/RPMS\n" unless @rpms == 1;
my $rpm = "$out/" . basename($rpms[0]);
copy($rpms[0], $rpm) or die "copy rpm: $!";

# 3. Tar-Archiv desselben Baums (fuer Hosts mit rpm.rte 3.0.5)
(my $tar = $rpm) =~ s/\.rpm$/.tar.gz/;
# Only opt/btop: unpacking usr/ entries as root under / would reset owner, mode and mtime of /usr and /usr/bin
# (AIX: bin:bin). The /usr/bin/btop symlink is a documented manual step for the tar route (README.AIX).
run('sh', '-c', "cd '$tree' && tar -cf - opt | gzip -9 > '$tar'");

# 4. Nachweis
my $info = "$out/rpm-info.txt";
# no checksums here: the RPM is signed later, which changes its checksum; SHA256SUMS of the release is authoritative
run('sh', '-c', "{ rpm -qpi '$rpm'; echo; rpm -qp --requires '$rpm'; echo; rpm -qpl '$rpm'; } > '$info' 2>&1");
open my $i, '<', $info or die; print <$i>; close $i;
# Abnahme R1: kein Eintrag im Paket darf fuer Gruppe oder andere schreibbar sein (Symlinks ausgenommen)
# Modusfeld = erstes Wort, die letzten 9 Zeichen (rwxrwxrwx; AIX tar -tv laesst den Typbuchstaben evtl. weg)
sub writable_entries {
    grep { my ($m) = /^(\S+)/; defined $m && $m !~ /^l/ && length($m) >= 9
           && (substr($m, -9, 9) =~ /^....w|^.......w/) } @_
}
my @rl = `rpm -qplv '$rpm'`; my @tl = `gzip -dc '$tar' | tar -tvf -`;
die "Abnahme R1: leere Liste (rpm -qplv: " . @rl . ", tar -tv: " . @tl . ")\n" unless @rl > 40 && @tl > 40;
my @gw = writable_entries(@rl);
die "RPM enthaelt gruppen-/weltschreibbare Eintraege:\n@gw" if @gw;
my @tw = writable_entries(@tl);
die "Tar enthaelt gruppen-/weltschreibbare Eintraege:\n@tw" if @tw;
print "\nOK: $rpm\n    $tar\n    $info\n";
