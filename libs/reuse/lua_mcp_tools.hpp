// __REUSABLE__
//
// lua_mcp_tools.hpp - MCP ToolHost backed by a Lua tool registry file (tools.lua).
//
// Who:     Martin Lonkwitz
// What:    reuse::mcp::LuaTools loads a registry file in a reuse::lua::Sandbox. The file returns a list of
//          tools { name, description, inputSchema, handler }; handler(args) gets the arguments as a Lua table
//          (lua_json_bridge.hpp) and returns a Lua table that becomes the tool result. load() validates the
//          whole file first and keeps the previous registry when the new one is invalid. tools_changed()
//          reloads automatically when the file's mtime/size changed (and request_reload() via the skeleton).
// When:    2026-10-19
// Why:     The tool registry and handlers live in a script;
//          a new tool is a script change, no rebuild; reload at runtime. Generalised from btop-aix
//          src/btop_mcp.cpp (LuaTools, same validation rules).
// Project: reusable; used by btop-aix (StdioServer) and further projects (StdioServer, Dispatcher)
// Dependencies: lua_sandbox.hpp, lua_json_bridge.hpp, mcp_dispatcher.hpp (ToolHost; the same type block is in
//          mcp_stdio_server.hpp, so both skeletons accept a LuaTools), json.hpp; Lua 5.4 compiled as C++.
//
// Usage:
//     reuse::lua::Sandbox sb({...});  bind_app_api(sb.get());
//     reuse::mcp::LuaTools tools(sb, "mcp/tools.lua", {.raw_results = true});
//     if (auto r = tools.load(); !r) die(r.error());
//     reuse::mcp::StdioServer srv({"name", "1.0"}, tools, std::cin, std::cout, log);
//     srv.run();
//
// Results: raw_results = false: the handler's table is the tool value (skeleton wraps it as text +
//     structuredContent). raw_results = true: the handler returns the complete CallToolResult
//     ({ content = array{ object{type="text", text=...} }, isError = true|nil }).
// Access: a tool may carry an optional string field `access` (e.g. a minimum role). With
//     LuaToolsOptions::authorize set, permitted(tool, caller) = authorize(access, caller); the skeleton then hides
//     the tool in tools/list and refuses tools/call (mcp_dispatcher.hpp, Caller). access_of() returns the field
//     (e.g. for generated docs). The field is not part of the tools/list entry.
// Errors: Lua errors in a handler -> ToolError::Failed with the message (isError result); unknown tool ->
//     ToolError::UnknownTool (JSON-RPC -32602 by the skeleton).
// Threads: not thread-safe; the skeleton calls it from its single worker thread only (the Sandbox must
//     only be used on that thread while the server runs).
// History: git log -- cpp/lua_mcp_tools.hpp

#ifndef REUSE_LUA_MCP_TOOLS_HPP
#define REUSE_LUA_MCP_TOOLS_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include "json.hpp"
#include "lua_sandbox.hpp"
#include "lua_json_bridge.hpp"
#include "mcp_dispatcher.hpp"	//? ToolHost (shared type block; StdioServer in mcp_stdio_server.hpp uses the same)

namespace reuse::mcp {

	struct LuaToolsOptions {
		bool raw_results = false;	//? see Results above
		bool watch_file = true;		//? tools_changed() reloads on a changed file (and announces listChanged)
		//? Access check (see Access above): the tool's `access` string ("" when the field is absent) and the
		//? caller of the request -> permitted. Empty function = every caller may use every tool.
		std::function<bool(std::string_view access, const Caller& caller)> authorize = {};
	};

	class LuaTools : public ToolHost {
	public:
		LuaTools(reuse::lua::Sandbox& sandbox, std::filesystem::path file, LuaToolsOptions opts = {})
		: sb(sandbox), file(std::move(file)), opts(opts) {}
		~LuaTools() override { luaL_unref(sb.get(), LUA_REGISTRYINDEX, registry_ref); }
		LuaTools(const LuaTools&) = delete;
		LuaTools& operator=(const LuaTools&) = delete;

		[[nodiscard]] auto path() const -> const std::filesystem::path& { return file; }

		//* (Re)load the registry file; on error the previous registry stays active.
		auto load() -> std::expected<void, std::string> {
			lua_State* L = sb.get();
			const int top = lua_gettop(L);
			auto fail = [&](std::string msg) -> std::expected<void, std::string> {
				lua_settop(L, top);
				return std::unexpected(file.string() + ": " + msg);
			};
			const Stamp stamp = stamp_of(file);	//? before reading: a write during the load triggers another reload
			if (auto r = sb.do_file(file); not r) return fail(r.error());
			if (not lua_istable(L, -1)) return fail("must return a list of tools");

			static const std::regex tool_name_re("[A-Za-z0-9_-]{1,64}");
			nlohmann::json new_listing = nlohmann::json::array();
			std::unordered_map<std::string, int> new_index;
			std::unordered_map<std::string, std::string> new_access;
			const auto n = static_cast<int>(luaL_len(L, -1));
			for (int i = 1; i <= n; ++i) {
				const std::string where = "tool #" + std::to_string(i);
				lua_rawgeti(L, -1, i);
				if (not lua_istable(L, -1)) return fail(where + " is not a table");
				lua_getfield(L, -1, "name");
				lua_getfield(L, -2, "description");
				lua_getfield(L, -3, "inputSchema");
				lua_getfield(L, -4, "handler");
				lua_getfield(L, -5, "access");
				if (not lua_isnil(L, -1) and lua_type(L, -1) != LUA_TSTRING) return fail(where + ": access must be a string");
				std::string access = lua_isnil(L, -1) ? std::string() : std::string(lua_tostring(L, -1));
				lua_pop(L, 1);
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
				new_access.emplace(name, std::move(access));
				lua_pop(L, 5);
			}
			luaL_unref(L, LUA_REGISTRYINDEX, registry_ref);
			registry_ref = luaL_ref(L, LUA_REGISTRYINDEX);	//? pops the validated list
			listing = std::move(new_listing);
			index = std::move(new_index);
			access = std::move(new_access);
			loaded = stamp;
			lua_settop(L, top);
			return {};
		}

		auto list_tools() -> nlohmann::json override { return listing; }

		//* The `access` string of a loaded tool ("" = field absent); nullopt = no such tool
		[[nodiscard]] auto access_of(const std::string& name) const -> std::optional<std::string> {
			const auto it = access.find(name);
			if (it == access.end()) return std::nullopt;
			return it->second;
		}

		//* Unknown tools are "permitted" here, so tools/call reports them as unknown (-32602), not as refused.
		auto permitted(const std::string& name, const Caller& caller) -> bool override {
			if (not opts.authorize) return true;
			const auto it = access.find(name);
			if (it == access.end()) return true;
			return opts.authorize(it->second, caller);
		}

		auto call_tool(const std::string& name, const nlohmann::json& arguments) -> std::expected<nlohmann::json, ToolError> override {
			const auto it = index.find(name);
			if (it == index.end()) return std::unexpected(ToolError{ToolError::Kind::UnknownTool, "unknown tool: " + name, {}});
			lua_State* L = sb.get();
			const int top = lua_gettop(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, registry_ref);
			lua_rawgeti(L, -1, it->second);
			lua_getfield(L, -1, "handler");
			if (not reuse::luajson::push(L, arguments)) {
				lua_settop(L, top);
				return std::unexpected(ToolError{ToolError::Kind::InvalidArguments, "arguments nested too deeply", {}});
			}
			if (auto r = sb.pcall(1, 1); not r) {
				lua_settop(L, top);
				return std::unexpected(ToolError{ToolError::Kind::Failed, r.error(), {}});
			}
			auto result = reuse::luajson::to_json(L, -1);
			lua_settop(L, top);
			if (not result) return std::unexpected(ToolError{ToolError::Kind::Failed, "result of '" + name + "': " + result.error(), {}});
			return *result;
		}

		auto reload() -> std::expected<void, std::string> override { return load(); }
		auto raw_results() const -> bool override { return opts.raw_results; }
		auto supports_list_changed() const -> bool override { return opts.watch_file; }

		//* Reload when the file changed since the last successful load; true = new registry active.
		//* A failed reload keeps the old registry and is retried only after the next change of the file.
		auto tools_changed() -> bool override {
			if (not opts.watch_file) return false;
			const Stamp now = stamp_of(file);
			if (not now.valid or now == loaded or now == failed) return false;
			if (auto r = load(); not r) {
				failed = now;
				last_error = r.error();
				return false;
			}
			return true;
		}

		//* Message of the last failed automatic reload (empty = none)
		[[nodiscard]] auto reload_error() const -> const std::string& { return last_error; }

	private:
		struct Stamp {
			std::filesystem::file_time_type mtime{};
			std::uintmax_t size = 0;
			bool valid = false;
			bool operator==(const Stamp&) const = default;
		};
		static auto stamp_of(const std::filesystem::path& p) -> Stamp {
			std::error_code ec1, ec2;
			const auto t = std::filesystem::last_write_time(p, ec1);
			const auto s = std::filesystem::file_size(p, ec2);
			if (ec1 or ec2) return {};
			return {t, s, true};
		}

		reuse::lua::Sandbox& sb;
		std::filesystem::path file;
		LuaToolsOptions opts;
		int registry_ref = LUA_NOREF;
		nlohmann::json listing = nlohmann::json::array();
		std::unordered_map<std::string, int> index;	//? tool name -> position in the Lua list (1-based)
		std::unordered_map<std::string, std::string> access;	//? tool name -> access string ("" = none)
		Stamp loaded, failed;
		std::string last_error;
	};

} // namespace reuse::mcp

#endif
