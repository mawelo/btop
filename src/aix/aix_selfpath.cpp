/* Copyright 2026 Martin Lonkwitz (btop-aix)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

tab-size = 4
*/

// aix_selfpath.cpp -- project btop-aix (2026-10-12), see aix_selfpath.hpp

#include "aix_selfpath.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>

#include <procinfo.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Aix {

	namespace {
		//* argv[0] of the own process via getprocs64/getargs; empty on failure
		auto own_argv0() -> std::string {
			struct procentry64 pe{};
			pid_t idx = getpid();
			if (getprocs64(&pe, sizeof pe, nullptr, 0, &idx, 1) != 1 or pe.pi_pid != getpid()) return {};
			std::array<char, 4096> args{};
			if (getargs(&pe, sizeof pe, args.data(), static_cast<int>(args.size())) != 0) return {};
			args.back() = '\0';
			return std::string{args.data()}; // first NUL-terminated string is argv[0]
		}

		//* first PATH entry holding an executable `name`; empty when none
		auto search_path(std::string_view name) -> fs::path {
			const char* path_env = std::getenv("PATH");
			if (path_env == nullptr) return {};
			std::string_view path{path_env};
			while (not path.empty()) {
				const auto colon = path.find(':');
				const auto dir = path.substr(0, colon);
				const fs::path candidate = (dir.empty() ? fs::path{"."} : fs::path{dir}) / name;
				if (access(candidate.c_str(), X_OK) == 0) return candidate;
				if (colon == std::string_view::npos) break;
				path.remove_prefix(colon + 1);
			}
			return {};
		}
	}

	auto executable_dir() -> fs::path {
		const std::string argv0 = own_argv0();
		if (argv0.empty()) return {};
		const fs::path found = (argv0.find('/') != std::string::npos) ? fs::path{argv0} : search_path(argv0);
		if (found.empty()) return {};
		std::error_code ec;
		fs::path resolved = fs::canonical(found, ec);
		if (ec) return {};
		return resolved.remove_filename();
	}
}
