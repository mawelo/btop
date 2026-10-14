#!/bin/sh
# btop launcher for AIX (project btop-aix, 2026-10-12)
# Installed as /opt/btop/bin/btop by the btop RPM; /usr/bin/btop points here.
# Why: AIX hosts rarely have a UTF-8 locale installed and btop refuses to start
# without one unless --force-utf is given. The launcher adds that option when no
# UTF-8 locale is active and sets TERM=xterm-256color when TERM is empty.
# btop.bin is taken from the launcher's own directory after following symlinks,
# so a tree unpacked anywhere (tar, without root) starts its own binary.
# All arguments are passed on unchanged to the real binary.
case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
	*[Uu][Tt][Ff]-8*|*[Uu][Tt][Ff]8*) ;;
	*) set -- --force-utf "$@" ;;
esac
if [ -z "$TERM" ]; then
	TERM=xterm-256color
	export TERM
fi
self=$0 n=0
# started as "sh btop" from PATH: $0 has no slash, look it up (re-review 2026-10-14, R7)
case $self in */*) ;; *) self=$(command -v "$self" 2>/dev/null) || self=$0 ;; esac
while [ -h "$self" ] && [ $n -lt 16 ]; do
	# strip "<mode> ... $self -> " literally, so a " -> " inside a path name does not cut the target (R7)
	l=$(ls -ld "$self")
	link=${l#*"$self -> "}
	case $link in /*) self=$link ;; *) self=$(dirname "$self")/$link ;; esac
	n=$((n + 1))
done
exec "$(dirname "$self")/btop.bin" "$@"
