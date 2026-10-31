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

Project : btop-aix (branch aix), 2026-10-19. See btop_mcp.hpp, btop_mcp_tools.hpp, docs/btop_mcp.md.
History : git log -- src/btop_mcp.cpp
*/

#include <atomic>
#include <csignal>
#include <iostream>
#include <regex>
#include <unordered_set>

#include "lua.h"
#include "lauxlib.h"

#include "lua_json_bridge.hpp"

#include "btop_config.hpp"
#include "btop_core.hpp"
#include "btop_log.hpp"
#include "btop_mcp.hpp"
#include "btop_mcp_tools.hpp"
#include "btop_shared.hpp"
#include "btop_theme.hpp"

namespace Mcp {

	namespace {
		//? Pointer for the SIGHUP handler; lock-free atomic, so the handler stays async-signal-safe
		std::atomic<reuse::mcp::StdioServer*> active_server{nullptr};
		static_assert(std::atomic<reuse::mcp::StdioServer*>::is_always_lock_free);

		void on_sighup(int) {
			if (auto* s = active_server.load(std::memory_order_relaxed)) s->request_reload();
		}

		const std::regex tool_name_re{"^[A-Za-z0-9_-]{1,64}$"};
	}

	LuaTools::LuaTools(Lua::State& state, std::filesystem::path file)
	: state(state), file(std::move(file)), registry_ref(LUA_NOREF) {}

	LuaTools::~LuaTools() {
		luaL_unref(state.get(), LUA_REGISTRYINDEX, registry_ref);
	}

	auto LuaTools::load() -> std::expected<void, std::string> {
		lua_State* L = state.get();
		const int top = lua_gettop(L);
		auto fail = [&](std::string msg) -> std::expected<void, std::string> {
			lua_settop(L, top);
			return std::unexpected(file.string() + ": " + msg);
		};
		if (auto r = state.do_file(file); not r) return fail(r.error());
		if (not lua_istable(L, -1)) return fail("must return a list of tools");

		nlohmann::json new_listing = nlohmann::json::array();
		std::unordered_map<std::string, int> new_index;
		const auto n = static_cast<int>(luaL_len(L, -1));
		for (int i = 1; i <= n; ++i) {
			const std::string where = "tool #" + std::to_string(i);
			lua_rawgeti(L, -1, i);
			if (not lua_istable(L, -1)) return fail(where + " is not a table");
			lua_getfield(L, -1, "name");
			lua_getfield(L, -2, "description");
			lua_getfield(L, -3, "inputSchema");
			lua_getfield(L, -4, "handler");
			if (lua_type(L, -4) != LUA_TSTRING) return fail(where + ": name must be a string");
			const std::string name = lua_tostring(L, -4);
			if (not std::regex_match(name, tool_name_re)) return fail(where + ": name '" + name + "' must match [A-Za-z0-9_-]{1,64}");
			if (new_index.contains(name)) return fail("tool '" + name + "' defined twice");
			if (lua_type(L, -3) != LUA_TSTRING) return fail("tool '" + name + "': description must be a string");
			if (not lua_isfunction(L, -1)) return fail("tool '" + name + "': handler must be a function");
			auto schema = reuse::luajson::to_json(L, -2);
			if (not schema) return fail("tool '" + name + "': inputSchema: " + schema.error());
			if (not schema->is_object() or schema->value("type", "") != "object")
				return fail("tool '" + name + "': inputSchema must be an object schema (type = \"object\")");
			new_listing.push_back({{"name", name}, {"description", lua_tostring(L, -3)}, {"inputSchema", *schema}});
			new_index.emplace(name, i);
			lua_pop(L, 5);
		}
		//? Valid: replace the previous registry (luaL_ref pops the list)
		luaL_unref(L, LUA_REGISTRYINDEX, registry_ref);
		registry_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		listing = std::move(new_listing);
		index = std::move(new_index);
		lua_settop(L, top);
		return {};
	}

	auto LuaTools::list_tools() -> nlohmann::json {
		return listing;
	}

	auto LuaTools::call_tool(const std::string& name, const nlohmann::json& arguments) -> std::expected<nlohmann::json, reuse::mcp::ToolError> {
		using Kind = reuse::mcp::ToolError::Kind;
		//* lib b266dd0 added ToolError::structured; btop never sends a structured error, so it stays null (explicit for -Wextra)
		const auto fail = [](Kind kind, std::string message) {
			return std::unexpected(reuse::mcp::ToolError{kind, std::move(message), nlohmann::json{}});
		};
		const auto it = index.find(name);
		if (it == index.end()) return fail(Kind::UnknownTool, "unknown tool: " + name);
		lua_State* L = state.get();
		const int top = lua_gettop(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, registry_ref);
		lua_rawgeti(L, -1, it->second);
		lua_getfield(L, -1, "handler");
		if (not reuse::luajson::push(L, arguments)) {
			lua_settop(L, top);
			return fail(Kind::InvalidArguments, "arguments nested too deeply");
		}
		if (auto r = state.pcall(1, 1); not r) {
			lua_settop(L, top);
			return fail(Kind::Failed, r.error());
		}
		auto result = reuse::luajson::to_json(L, -1);
		lua_settop(L, top);
		if (not result) return fail(Kind::Failed, "result of '" + name + "': " + result.error());
		return *result;
	}

	auto default_tools_file() -> std::filesystem::path {
		if (Theme::theme_dir.empty()) return {};
		return Theme::theme_dir.parent_path() / "mcp" / "tools.lua";
	}

	auto run_stdio(const Options& opts) -> int {
		if (opts.tools_file.empty()) {
			std::cerr << "btop --mcp-stdio: no tool registry found; pass --mcp-tools FILE\n";
			return 1;
		}
		//? The collectors read these keys; a selection or filter saved by the TUI must not shape the data
		Config::set("proc_tree", false);
		Config::set("proc_filter", std::string{});
		Config::set("pause_proc_list", false);
		Config::set("show_detailed", false);
		try {
			Shared::init();
		}
		catch (const std::exception& e) {
			std::cerr << "btop --mcp-stdio: Shared::init: " << e.what() << '\n';
			return 1;
		}

		Core::Loop loop(opts.interval_ms);
		loop.start();
		Lua::State state({
			.snapshot = [&loop] { return loop.snapshot(); },
			.send_signal = [&loop](pid_t pid, int sig) { return loop.call([pid, sig] { return Core::send_signal(pid, sig); }); },
			.allow_signals = opts.allow_signals,
		});
		LuaTools tools(state, opts.tools_file);
		if (auto r = tools.load(); not r) {
			std::cerr << "btop --mcp-stdio: " << r.error() << '\n';
			loop.stop();
			return 1;
		}
		//? CPU percent is a difference of two collections: serve from the second snapshot on
		loop.wait_for_seq(2, 2 * opts.interval_ms + 10000);

		reuse::mcp::StdioServer server(
			{"btop", Lua::version(), "Read-only system measurements of this host (btop core API via Lua). "
				"Values refresh every " + std::to_string(opts.interval_ms) + " ms; snapshot_info tells the age."},
			tools, std::cin, std::cout,
			[](std::string_view m) { Logger::info("mcp: {}", m); });
		active_server.store(&server);
		std::signal(SIGHUP, on_sighup);
		Logger::info("mcp: serving {} on stdio", opts.tools_file.string());
		const int rc = server.run();
		std::signal(SIGHUP, SIG_DFL);
		active_server.store(nullptr);
		loop.stop();
		return rc;
	}

} // namespace Mcp
