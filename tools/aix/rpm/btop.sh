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
while [ -h "$self" ] && [ $n -lt 16 ]; do
	link=$(ls -ld "$self" | sed 's/.* -> //')
	case $link in /*) self=$link ;; *) self=$(dirname "$self")/$link ;; esac
	n=$((n + 1))
done
exec "$(dirname "$self")/btop.bin" "$@"
