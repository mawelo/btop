// cxxrt_probe.cpp -- Projekt btop-aix (2026-10-12)
// Zweck: Prueft, ob ein mit Open XL 17.1.3 (libc++ 19) auf AIX 7.2 gebautes
// C++23-Programm auf AIX 7.1 laeuft, wenn die libc++-Laufzeit (libc++.a,
// libc++abi.a, libunwind.a) ueber LIBPATH mitgegeben wird. Uebt die Teile,
// die btop braucht: Threads, Exceptions, iostream, std::expected, ranges::to,
// string::contains, optional-Monaden, chrono, filesystem, clock_gettime.
// Bauen (AIX-7.2-Build-Host mit Open XL): /opt/IBM/openxlC/17.1.3/bin/ibm-clang++_r -m64 -std=c++23 -O1
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
	if (s.empty()) return std::unexpected("leer");
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
