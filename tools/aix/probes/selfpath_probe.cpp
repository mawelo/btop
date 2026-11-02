// selfpath_probe.cpp -- project btop-aix (2026-10-19)
//
// Who/what: Prints the steps of Aix::executable_dir() (src/aix/aix_selfpath.cpp): getprocs64 result, PID found,
//           getargs return value, argv[0], canonical(argv[0]) and canonical(dir/../share/btop/themes).
// Why:      btop --mcp-stdio did not find its tool registry on AIX 7200-05, 6100-09 and 6100-07 (Theme::theme_dir
//           empty), but did on 7100-02 and 7300-04. Which step fails? (Answer: canonical, see realpath_probe.c.)
// Build:    (AIX 7.2 build host with Open XL) /opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r -std=c++23 -D_ALL_SOURCE -O1
//           -o sp selfpath_probe.cpp
//           ./sp ; (cd /; /tmp/x/sp)   -- call it with a relative and with an absolute path
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include <procinfo.h>
#include <sys/types.h>
#include <unistd.h>

int main(int, char** argv) {
	std::printf("argv[0] (main): %s, getpid %d\n", argv[0], static_cast<int>(getpid()));
	struct procentry64 pe{};
	pid_t idx = getpid();
	errno = 0;
	const int n = getprocs64(&pe, sizeof pe, nullptr, 0, &idx, 1);
	std::printf("getprocs64 rc=%d errno=%d pi_pid=%d next idx=%d\n", n, errno, static_cast<int>(pe.pi_pid), static_cast<int>(idx));
	std::array<char, 4096> args{};
	errno = 0;
	const int g = getargs(&pe, sizeof pe, args.data(), static_cast<int>(args.size()));
	args.back() = '\0';
	std::printf("getargs rc=%d errno=%d argv0='%s'\n", g, errno, args.data());
	std::error_code ec;
	const auto c = std::filesystem::canonical(args.data(), ec);
	std::printf("canonical(argv0) = '%s' ec=%d (%s)\n", c.c_str(), ec.value(), ec.message().c_str());
	const auto t = std::filesystem::canonical(c.parent_path() / "../share/btop/themes", ec);
	std::printf("canonical(themes) = '%s' ec=%d (%s)\n", t.c_str(), ec.value(), ec.message().c_str());
	return 0;
}
