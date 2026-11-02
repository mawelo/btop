/* Copyright 2026 Martin Lonkwitz (martin@lonkwitz.eu)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4

Project : btop-aix (branch aix), 2026-10-19
What    : MCP adapter entry: btop --mcp-stdio runs headless (no terminal), collects in Core::Loop and serves the
          tools of a Lua registry (mcp/tools.lua) over stdio. No domain logic here: tools live in Lua, data in the core.
Why     : MCP clients query the measurements of a host; the adapter holds no domain logic of its own.
Deps    : btop_mcp_tools.hpp, btop_core.hpp. Docs: docs/btop_mcp.md, docs/mcp.md (generated)
Usage   : ssh <user>@<host> /opt/btop/bin/btop --mcp-stdio [--mcp-tools FILE] [--mcp-allow-signals]
History : git log -- src/btop_mcp.hpp
*/

#pragma once

#include <cstdint>
#include <filesystem>

namespace Mcp {

	struct Options {
		std::filesystem::path tools_file;	//? Lua registry, see mcp/tools.lua
		bool allow_signals = false;
		uint64_t interval_ms = 2000;
	};

	//* Headless MCP server on stdin/stdout until EOF; returns the process exit code. Config must be loaded.
	auto run_stdio(const Options& opts) -> int;

	//* Default registry path: <theme dir>/../mcp/tools.lua (/opt/btop/share/btop/mcp/tools.lua in the package)
	auto default_tools_file() -> std::filesystem::path;

} // namespace Mcp
