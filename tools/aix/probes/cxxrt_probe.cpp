// cxxrt_probe.cpp -- project btop-aix (2026-10-12)
// Who/what: Checks whether a C++23 program built with Open XL 17.1.3 (libc++ 19) on AIX 7.2
// runs on AIX 7.1 when the libc++ runtime (libc++.a,
// libc++abi.a, libunwind.a) is supplied through LIBPATH. Exercises the parts
// btop needs: threads, exceptions, iostream, std::expected, ranges::to,
// string::contains, optional monads, chrono, filesystem, clock_gettime.
// Build (AIX 7.2 build host with Open XL): /opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r -m64 -std=c++23 -O1
//                  -pthread -o cxxrt_probe cxxrt_probe.cpp
#include <chrono>
#include <ctime>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <version>

static std::expected<int, std::string> parse(const std::string& s) {
	if (s.empty()) return std::unexpected("empty");
	return std::stoi(s);
}

int main() {
	std::cout << "_LIBCPP_VERSION=" << _LIBCPP_VERSION << " __cplusplus=" << __cplusplus << "\n";
	auto v = std::views::iota(1, 6) | std::ranges::to<std::vector<int>>();
	std::string s = "btop on aix";
	std::optional<int> o = 20;
	auto o2 = o.and_then([](int x) { return std::optional<int>{x * 2}; });
	std::cout << "ranges::to size=" << v.size() << " contains=" << s.contains("aix") << " monad=" << o2.value_or(0) << "\n";
	std::cout << "expected ok=" << parse("42").value() << " err=" << parse("").error() << "\n";
	int sum = 0;
	std::thread t([&] { for (int i : v) sum += i; });
	t.join();
	std::cout << "thread sum=" << sum << "\n";
	try { throw std::runtime_error("exception ok"); } catch (const std::exception& e) { std::cout << e.what() << "\n"; }
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	std::cout << "monotonic=" << ts.tv_sec << " fs_exists(/etc)=" << std::filesystem::exists("/etc") << "\n";
	std::cout << "steady=" << std::chrono::steady_clock::now().time_since_epoch().count() << "\n";
	std::cout << "OK\n";
	return 0;
}
