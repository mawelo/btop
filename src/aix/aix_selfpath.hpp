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

// aix_selfpath.hpp -- project btop-aix (2026-10-12)
// What: directory of the running executable on AIX, used to find ../share/btop/themes
//       next to the binary (Linux reads /proc/self/exe, macOS _NSGetExecutablePath,
//       NetBSD sysctl; AIX has none of these).
// How : getprocs64(3) + getargs(3) give argv[0] of the own process; a name with a
//       slash is resolved against the working directory, a bare name is searched in PATH.
// Dependencies: libc only (procinfo.h). Used by src/btop.cpp.

#pragma once

#include <filesystem>

namespace Aix {
	//* Directory of the running executable with trailing separator (like fs::path::remove_filename()),
	//* empty when it cannot be determined (the caller then falls back to /usr/share/btop/themes).
	[[nodiscard]] auto executable_dir() -> std::filesystem::path;
}
