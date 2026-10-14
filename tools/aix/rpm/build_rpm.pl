#!/usr/bin/perl
# build_rpm.pl -- project btop-aix (2026-10-12)
#
# Who/what: Runs on the AIX build host and builds the binary RPM and an equivalent tar archive of btop from a
#           finished binary and the source tree (themes, README, LICENSE). The file tree (/opt/btop, /usr/bin/btop)
#           is laid out ONCE; spec and tar take it over unchanged (one source for the layout).
# When    : 2026-10-12.
# Why     : One installable package for all AIX levels from 6.1 TL7 on (decision 2026-10-12: only the 6.1 build is
#           shipped, it runs up to 7.3), traceable to commit, binary and spec (tools/aix/rpm/btop.spec, launcher
#           tools/aix/rpm/btop.sh).
# Usage   : perl tools/aix/rpm/build_rpm.pl --binary build/aix61-openxl/btop --source . \
#               --commit <id> --out <dir> [--release 1] [--target ppc-ibm-aix6.1] [--topdir DIR]
#           --target sets the rpm OS field and the file name (btop-<ver>-<rel>.aix6.1.ppc.rpm): aix6.1 is the minimum
#           level; rpm on 7.1/7.2/7.3 accepts it through its compatibility table (measured 2026-10-12), a bare "aix"
#           is rejected ("intended for a different operating system").
#           Needs AIX with rpm.rte 4.x (rpmbuild) and the system Perl; no CPAN modules.
# Output  : <out>/btop-<ver>-<rel>.<os>.ppc.rpm, <out>/btop-<ver>-<rel>.<os>.ppc.tar.gz,
#           <out>/rpm-info.txt (rpm -qpi/-qpl/--requires; no checksum, see below). rc 0 only after a complete run.
use strict;
use warnings;
use Getopt::Long;
use File::Path qw(make_path remove_tree);
use File::Copy qw(copy);
use File::Basename qw(basename);
use Cwd qw(abs_path getcwd);

my %o = (release => '1', target => 'ppc-ibm-aix6.1', spec => '');
GetOptions(\%o, 'binary=s', 'source=s', 'commit=s', 'out=s', 'release=s', 'target=s', 'topdir=s', 'spec=s')
    or die "usage: $0 --binary FILE --source DIR --commit ID --out DIR [--release N] [--target TRIPLE] [--topdir DIR]\n";
for (qw(binary source commit out)) { die "--$_ missing\n" unless defined $o{$_} && length $o{$_} }
die "$o{binary}: not an executable binary\n" unless -f $o{binary} && -x _;
die "$o{source}: not a btop source tree (themes/ missing)\n" unless -d "$o{source}/themes";

my $src    = abs_path($o{source});
my $binary = abs_path($o{binary});
make_path($o{out});
my $out    = abs_path($o{out});
my $topdir = $o{topdir} ? $o{topdir} : "$out/rpmbuild";
my $spec   = $o{spec} || "$src/tools/aix/rpm/btop.spec";
my $launch = "$src/tools/aix/rpm/btop.sh";
my $readme = "$src/tools/aix/rpm/README.AIX";
for ($spec, $launch, $readme, "$src/README.md", "$src/LICENSE") { die "$_: missing\n" unless -f $_ }

# version from the source tree (Global::Version in src/btop.cpp), never typed in
my $version = '';
open my $vf, '<', "$src/src/btop.cpp" or die "src/btop.cpp: $!";
while (<$vf>) { if (/const\s+string\s+Version\s*=\s*"([^"]+)"/) { $version = $1; last } }
close $vf;
die "version not found in src/btop.cpp\n" unless $version;
(my $target_os = $o{target}) =~ s/^.*-(aix[0-9.]*).*$/$1/;

sub run { print "+ @_\n"; system(@_) == 0 or die "FAILED (rc=" . ($? >> 8) . "): @_\n" }

# 1. file tree (the one source for RPM and tar)
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
# modes independent of the build host umask (release 2 shipped themes 0664 and directories 0775 because cp -Rp
# copied the modes of the checkout). Applies to RPM (plus %defattr in the spec) and tar.
run('chmod', '-R', 'u=rwX,go=rX', "$tree/opt");
printf "tree: %s (version %s, release %s, commit %s, target %s)\n", $tree, $version, $o{release}, $o{commit}, $o{target};

# 2. RPM
run('rpmbuild', '-bb', '--target', $o{target},
    '--define', "_topdir $topdir", '--define', "btop_tree $tree",
    '--define', "btop_version $version", '--define', "btop_release $o{release}",
    '--define', "btop_commit $o{commit}", '--define', "btop_target $target_os", $spec);
my @rpms = glob("$topdir/RPMS/*/btop-$version-$o{release}*.rpm");
die "no single RPM under $topdir/RPMS\n" unless @rpms == 1;
my $rpm = "$out/" . basename($rpms[0]);
copy($rpms[0], $rpm) or die "copy rpm: $!";

# 3. tar archive of the same tree (for hosts with rpm.rte 3.0.5)
(my $tar = $rpm) =~ s/\.rpm$/.tar.gz/;
# Only opt/btop: unpacking usr/ entries as root under / would reset owner, mode and mtime of /usr and /usr/bin
# (AIX: bin:bin). The /usr/bin/btop symlink is a documented manual step for the tar route (README.AIX).
run('sh', '-c', "cd '$tree' && tar -cf - opt | gzip -9 > '$tar'");

# 4. record
my $info = "$out/rpm-info.txt";
# no checksums here: the RPM is signed later, which changes its checksum; SHA256SUMS of the release is authoritative
run('sh', '-c', "{ rpm -qpi '$rpm'; echo; rpm -qp --requires '$rpm'; echo; rpm -qpl '$rpm'; } > '$info' 2>&1");
open my $i, '<', $info or die; print <$i>; close $i;
# acceptance check: no entry of the package may be writable by group or others (symlinks excepted).
# mode field = first word, its last 9 characters (rwxrwxrwx; AIX tar -tv may omit the type letter)
sub writable_entries {
    grep { my ($m) = /^(\S+)/; defined $m && $m !~ /^l/ && length($m) >= 9
           && (substr($m, -9, 9) =~ /^....w|^.......w/) } @_
}
my @rl = `rpm -qplv '$rpm'`; my @tl = `gzip -dc '$tar' | tar -tvf -`;
die "mode check: listing too short (rpm -qplv: " . @rl . ", tar -tv: " . @tl . ")\n" unless @rl > 40 && @tl > 40;
my @gw = writable_entries(@rl);
die "RPM has group/world-writable entries:\n@gw" if @gw;
my @tw = writable_entries(@tl);
die "tar has group/world-writable entries:\n@tw" if @tw;
print "\nOK: $rpm\n    $tar\n    $info\n";
