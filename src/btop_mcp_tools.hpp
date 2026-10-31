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
What    : ToolHost for the reusable MCP skeleton, backed by a Lua registry file. The file returns a list of
          {name, description, inputSchema, handler}; handlers run in the sandboxed Lua::State with its time limit.
          load() validates the whole file first and keeps the previous registry when the new one is invalid
          (reload at runtime via SIGHUP).
Why     : Code rule "App-Architektur: Scripting-API & MCP": a new tool is a script change, not a rebuild.
Threads : Not thread-safe; the skeleton calls it from its single worker thread only.
History : git log -- src/btop_mcp_tools.hpp
*/

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "mcp_stdio_server.hpp"

#include "btop_lua.hpp"

namespace Mcp {

	class LuaTools : public reuse::mcp::ToolHost {
	public:
		LuaTools(Lua::State& state, std::filesystem::path file);
		~LuaTools() override;
		LuaTools(const LuaTools&) = delete;
		LuaTools& operator=(const LuaTools&) = delete;

		//* (Re)load the registry file; on error the previous registry stays active.
		auto load() -> std::expected<void, std::string>;

		auto list_tools() -> nlohmann::json override;
		auto call_tool(const std::string& name, const nlohmann::json& arguments) -> std::expected<nlohmann::json, reuse::mcp::ToolError> override;
		auto reload() -> std::expected<void, std::string> override { return load(); }

	private:
		Lua::State& state;
		std::filesystem::path file;
		int registry_ref;	//? luaL_ref of the validated tool list, LUA_NOREF before the first load
		nlohmann::json listing = nlohmann::json::array();
		std::unordered_map<std::string, int> index;	//? tool name -> position in the Lua list (1-based)
	};

} // namespace Mcp
