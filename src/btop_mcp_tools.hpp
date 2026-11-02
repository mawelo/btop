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

Project : btop-aix (branch aix), 2026-10-19; since 2026-10-31 a thin layer over a shared header
What    : Mcp::LuaTools = reuse::mcp::LuaTools (libs/reuse/lua_mcp_tools.hpp) on the sandbox of a Lua::State, with
          btop's options in one place: the registry file is reloaded only on request (SIGHUP ->
          StdioServer::request_reload), not when its mtime changes, and the skeleton wraps handler results
          (text + structuredContent). The registry file returns a list of {name, description, inputSchema,
          handler}; load() validates the whole file first and keeps the previous registry when the new one is
          invalid.
Why     : A new tool is a script change, not a rebuild. The tool host is the shared one, not a fork (until 2026-10-31 btop had
          its own copy in src/btop_mcp.cpp).
Threads : Not thread-safe; the skeleton calls it from its single worker thread only.
History : git log -- src/btop_mcp_tools.hpp
*/

#pragma once

#include <filesystem>
#include <utility>

#include "mcp_stdio_server.hpp"
#include "lua_mcp_tools.hpp"

#include "btop_lua.hpp"

namespace Mcp {

	//* btop's options for the shared tool host (see the file head)
	inline auto tools_options() -> reuse::mcp::LuaToolsOptions {
		return {.raw_results = false, .watch_file = false, .authorize = {}};
	}

	class LuaTools : public reuse::mcp::LuaTools {
	public:
		LuaTools(Lua::State& state, std::filesystem::path file)
		: reuse::mcp::LuaTools(state.sandbox(), std::move(file), tools_options()) {}
	};

} // namespace Mcp
