# btop.spec -- AIX package for btop (project btop-aix, 2026-10-12)
#
# What : Binary RPM for AIX built from an already compiled btop binary and the source tree.
#        Layout: /opt/btop/bin/btop (launcher), /opt/btop/bin/btop.bin (binary),
#        /opt/btop/share/btop/themes, /opt/btop/share/doc/btop, symlink /usr/bin/btop.
# Why  : AIX has no C++23 compiler on 7.1 and older hosts; the binary is built on an AIX 7.2
#        host with IBM Open XL C/C++ against the AIX 6.1 sysroot (tools/aix/sysroot71) and runs
#        on every release from 6.1 TL7 to 7.3; one package for all of them (decision 2026-10-12).
# How  : driven by tools/aix/rpm/build_rpm.pl, which stages the file tree and passes
#        btop_tree, btop_version, btop_release, btop_commit, btop_target as macros.
# Compat: MD5 file digests and gzip payload so that rpm.rte 4.9 (AIX 6.1/7.1 Toolbox) can
#        read the package; rpm.rte 3.0.5 cannot (payload prefix), use the tar archive there.

%{!?btop_version: %{error: btop_version is not defined (build_rpm.pl passes it)}}
%{!?btop_tree:    %{error: btop_tree is not defined (build_rpm.pl passes it)}}
%{!?btop_release: %define btop_release 1}
%{!?btop_commit:  %define btop_commit unknown}
%{!?btop_target:  %define btop_target aix6.1}

%define _binary_payload w9.gzdio
%define _binary_filedigest_algorithm 1
%define _source_filedigest_algorithm 1
%define __os_install_post %{nil}
%define debug_package %{nil}
%define _buildhost aix-btop-build

Name:           btop
Version:        %{btop_version}
Release:        %{btop_release}
Summary:        Resource monitor for the terminal (AIX port)
License:        Apache-2.0
Group:          Applications/System
URL:            https://github.com/aristocratos/btop
Vendor:         aix-btop project
AutoReqProv:    no

%description
btop shows CPU, memory, disk, network and process usage in a terminal UI.
This package carries the AIX port (perfstat, mntctl, getprocs64), built from
btop commit %{btop_commit} with IBM Open XL C/C++ against the AIX 6.1 headers and
libraries; it runs on AIX 6.1 TL7 and later, 7.1, 7.2 and 7.3 (the perfstat structure
size is probed at run time).
The binary links the C++ runtime statically and imports only libc, libpthreads
and libperfstat. A UTF-8 locale is not required: /opt/btop/bin/btop adds
--force-utf when none is active. Themes live in /opt/btop/share/btop/themes,
the per user configuration in ~/.config/btop.

%pre
# refuse to overwrite a foreign /usr/bin/btop (AIX Toolbox package, a local build, also a dangling symlink); ours is a symlink to /opt/btop/bin/btop
if { [ -e /usr/bin/btop ] || [ -L /usr/bin/btop ]; } && [ "$(ls -ld /usr/bin/btop 2>/dev/null | awk '{print $NF}')" != /opt/btop/bin/btop ]; then
	echo "btop: /usr/bin/btop exists and is not the symlink of this package; remove or rename it first" >&2
	exit 1
fi
exit 0

%prep
%build

%install
rm -rf "%{buildroot}"
mkdir -p "%{buildroot}"
# tar instead of cp: AIX cp -R follows the (dangling on the build host) symlink /usr/bin/btop
(cd "%{btop_tree}" && tar -cf - .) | (cd "%{buildroot}" && tar -xf -)

%clean
rm -rf "%{buildroot}"

%files
# fixed modes instead of the build host umask (re-review 2026-10-14, R1: release 2 shipped themes 0664/0775)
%defattr(0644,root,system,0755)
%dir /opt/btop
%dir /opt/btop/bin
%attr(0755,root,system) /opt/btop/bin/btop
%attr(0755,root,system) /opt/btop/bin/btop.bin
%dir /opt/btop/share
%dir /opt/btop/share/btop
/opt/btop/share/btop/themes
%dir /opt/btop/share/doc
%docdir /opt/btop/share/doc/btop
/opt/btop/share/doc/btop
/usr/bin/btop

%changelog
* Mon Oct 12 2026 aix-btop project - %{btop_version}-%{btop_release}
- AIX port of btop: perfstat, mntctl, getprocs64; static libc++ built against AIX 6.1, one package for 6.1 TL7 to 7.3
