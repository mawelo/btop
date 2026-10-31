// __REUSABLE__
//
// lua_json_bridge.hpp - convert between nlohmann::json and Lua 5.4 values.
//
// Who:     Martin Lonkwitz
// What:    push(L, json) puts a JSON value on the Lua stack; to_json(L, idx) reads a Lua value back.
//          Objects and arrays carry a marker metatable, so an empty table keeps its kind on the way
//          back (an empty JSON object must not turn into []). open_helpers() adds object(t), array(t)
//          and null to a Lua module table, so Lua code can mark its own tables and store a JSON null
//          as a table value (a nil value would drop the key).
// When:    2026-10-19
// Why:     MCP tool registries written in Lua (code rule "App-Architektur: Scripting-API & MCP"):
//          arguments arrive as JSON, handlers return Lua tables, results leave as JSON.
// Project: reusable, originally btop-aix (mcp/tools.lua)
// Dependencies: Lua 5.4 headers, nlohmann/json 3.x (json.hpp in this directory), C++23 (std::expected).
//          Include the Lua headers BEFORE this file: "lua.h" + "lauxlib.h" when Lua is compiled as C++
//          (recommended: lua_error then unwinds C++ frames instead of longjmp over them), <lua.hpp> when
//          Lua is compiled as C.
//
// Usage:
//     #include "lua.h"
//     #include "lauxlib.h"
//     #include "json.hpp"
//     #include "lua_json_bridge.hpp"
//     reuse::luajson::push(L, nlohmann::json{{"limit", 5}});       // table on the stack
//     auto j = reuse::luajson::to_json(L, -1);                     // std::expected<json, std::string>
//     lua_newtable(L); reuse::luajson::open_helpers(L, lua_gettop(L));   // module.object / module.array
//
// Mapping Lua -> JSON:
//     nil -> null, module.null (light userdata sentinel) -> null, boolean -> boolean, integer -> integer, float -> number (NaN/inf: error),
//     string -> string (bytes unchanged; serialize with error_handler_t::replace for invalid UTF-8),
//     table with object marker -> object, with array marker -> array, without marker: keys 1..n -> array,
//     empty -> object, string keys -> object; mixed or other keys -> error. function/userdata/thread -> error.
// Mapping JSON -> Lua: null -> nil (unchanged; arguments rarely carry null), see push().
//
// Correctness: Depth limit (64) stops cycles; lua_checkstack before every push; the stack is balanced on
//     return of every function (also on error). Tests: btop-aix tests/lua_api.cpp (round trip, empty
//     object/array, NaN, cycle).
// Security: No Lua code runs here; no allocation outside Lua and nlohmann::json. Exceptions from
//     nlohmann::json (out of memory) pass through.
// History: git log -- cpp/lua_json_bridge.hpp

#ifndef REUSE_LUA_JSON_BRIDGE_HPP
#define REUSE_LUA_JSON_BRIDGE_HPP

#include <cmath>
#include <cstddef>
#include <expected>
#include <string>

#include "json.hpp"

namespace reuse::luajson {

	inline constexpr const char* object_marker = "reuse.luajson.object";
	inline constexpr const char* array_marker = "reuse.luajson.array";
	inline constexpr int max_depth = 64;

	//* Address used as the JSON-null sentinel (light userdata); one per program, compared by address.
	inline const char null_sentinel = 0;
	inline void push_null(lua_State* L) { lua_pushlightuserdata(L, const_cast<char*>(&null_sentinel)); }

	namespace detail {
		inline void set_marker(lua_State* L, const char* marker) {
			luaL_newmetatable(L, marker);	//? creates it once in the registry, pushes it either way
			lua_setmetatable(L, -2);
		}

		//? Returns object_marker, array_marker or nullptr for the table at idx
		inline auto marker_of(lua_State* L, int idx) -> const char* {
			if (not lua_getmetatable(L, idx)) return nullptr;
			const char* found = nullptr;
			luaL_getmetatable(L, object_marker);
			if (lua_rawequal(L, -1, -2)) found = object_marker;
			lua_pop(L, 1);
			if (found == nullptr) {
				luaL_getmetatable(L, array_marker);
				if (lua_rawequal(L, -1, -2)) found = array_marker;
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
			return found;
		}

		inline auto push(lua_State* L, const nlohmann::json& j, int depth) -> bool {
			if (depth > max_depth or not lua_checkstack(L, 3)) return false;
			switch (j.type()) {
			case nlohmann::json::value_t::null: lua_pushnil(L); return true;
			case nlohmann::json::value_t::boolean: lua_pushboolean(L, j.get<bool>()); return true;
			case nlohmann::json::value_t::number_integer: lua_pushinteger(L, j.get<lua_Integer>()); return true;
			case nlohmann::json::value_t::number_unsigned: {
				const auto u = j.get<unsigned long long>();
				if (u > static_cast<unsigned long long>(LUA_MAXINTEGER)) lua_pushnumber(L, static_cast<lua_Number>(u));
				else lua_pushinteger(L, static_cast<lua_Integer>(u));
				return true;
			}
			case nlohmann::json::value_t::number_float: lua_pushnumber(L, j.get<double>()); return true;
			case nlohmann::json::value_t::string: {
				const auto& s = j.get_ref<const std::string&>();
				lua_pushlstring(L, s.data(), s.size());
				return true;
			}
			case nlohmann::json::value_t::array: {
				lua_createtable(L, static_cast<int>(j.size()), 0);
				lua_Integer i = 1;
				for (const auto& v : j) {
					if (not push(L, v, depth + 1)) { lua_pop(L, 1); return false; }
					lua_rawseti(L, -2, i++);
				}
				set_marker(L, array_marker);
				return true;
			}
			case nlohmann::json::value_t::object: {
				lua_createtable(L, 0, static_cast<int>(j.size()));
				for (const auto& [k, v] : j.items()) {
					lua_pushlstring(L, k.data(), k.size());
					if (not push(L, v, depth + 1)) { lua_pop(L, 2); return false; }
					lua_rawset(L, -3);
				}
				set_marker(L, object_marker);
				return true;
			}
			default:	//? binary, discarded
				lua_pushnil(L);
				return true;
			}
		}

		inline auto to_json(lua_State* L, int idx, int depth) -> std::expected<nlohmann::json, std::string> {
			if (depth > max_depth) return std::unexpected("nesting deeper than 64 levels (cycle?)");
			idx = lua_absindex(L, idx);
			switch (lua_type(L, idx)) {
			case LUA_TNIL: return nlohmann::json(nullptr);
			case LUA_TBOOLEAN: return nlohmann::json(static_cast<bool>(lua_toboolean(L, idx)));
			case LUA_TNUMBER:
				if (lua_isinteger(L, idx)) return nlohmann::json(static_cast<long long>(lua_tointeger(L, idx)));
				if (const double d = lua_tonumber(L, idx); std::isfinite(d)) return nlohmann::json(d);
				return std::unexpected("NaN or infinity has no JSON form");
			case LUA_TSTRING: {
				size_t len = 0;
				const char* s = lua_tolstring(L, idx, &len);
				return nlohmann::json(std::string(s, len));
			}
			case LUA_TTABLE: break;
			case LUA_TLIGHTUSERDATA:
				if (lua_touserdata(L, idx) == &null_sentinel) return nlohmann::json(nullptr);
				return std::unexpected("no JSON form for a Lua light userdata");
			default: return std::unexpected(std::string("no JSON form for a Lua ") + luaL_typename(L, idx));
			}
			if (not lua_checkstack(L, 4)) return std::unexpected("Lua stack exhausted");

			const char* marker = marker_of(L, idx);
			//? Count keys and check whether they form 1..n
			size_t count = 0;
			bool all_int = true, all_str = true;
			lua_Integer max_key = 0;
			lua_pushnil(L);
			while (lua_next(L, idx) != 0) {
				++count;
				if (lua_type(L, -2) == LUA_TSTRING) all_int = false;
				else if (lua_isinteger(L, -2) and lua_tointeger(L, -2) >= 1) {
					all_str = false;
					if (lua_tointeger(L, -2) > max_key) max_key = lua_tointeger(L, -2);
				}
				else { all_int = all_str = false; }
				lua_pop(L, 1);
			}
			const bool as_array = marker == array_marker
				or (marker == nullptr and count > 0 and all_int and static_cast<size_t>(max_key) == count);
			if (as_array) {
				if (not all_int or (count > 0 and static_cast<size_t>(max_key) != count))
					return std::unexpected("array table with keys other than 1..n");
				auto arr = nlohmann::json::array();
				for (lua_Integer i = 1; i <= max_key; ++i) {
					lua_rawgeti(L, idx, i);
					auto v = to_json(L, -1, depth + 1);
					lua_pop(L, 1);
					if (not v) return v;
					arr.push_back(std::move(*v));
				}
				return arr;
			}
			if (not all_str) return std::unexpected("object table with non-string keys");
			auto obj = nlohmann::json::object();
			lua_pushnil(L);
			while (lua_next(L, idx) != 0) {
				size_t len = 0;
				const char* k = lua_tolstring(L, -2, &len);	//? key is a string (checked above), no conversion in place
				auto v = to_json(L, -1, depth + 1);
				lua_pop(L, 1);
				if (not v) { lua_pop(L, 1); return v; }
				obj[std::string(k, len)] = std::move(*v);
			}
			return obj;
		}

		inline auto mark(lua_State* L, const char* marker) -> int {
			if (lua_isnoneornil(L, 1)) lua_newtable(L);
			luaL_checktype(L, 1, LUA_TTABLE);
			lua_settop(L, 1);
			set_marker(L, marker);
			return 1;
		}
		inline auto lua_object(lua_State* L) -> int { return mark(L, object_marker); }
		inline auto lua_array(lua_State* L) -> int { return mark(L, array_marker); }
	} // namespace detail

	//* Push <j> onto the Lua stack. False (stack unchanged) when nesting exceeds max_depth or the stack is full.
	inline auto push(lua_State* L, const nlohmann::json& j) -> bool { return detail::push(L, j, 0); }

	//* Convert the Lua value at <idx> to JSON; the stack is unchanged.
	inline auto to_json(lua_State* L, int idx) -> std::expected<nlohmann::json, std::string> {
		const int top = lua_gettop(L);
		auto r = detail::to_json(L, idx, 0);
		lua_settop(L, top);
		return r;
	}

	//* Mark the table on top of the stack as JSON array / object (keeps its kind when empty).
	inline void mark_array(lua_State* L) { detail::set_marker(L, array_marker); }
	inline void mark_object(lua_State* L) { detail::set_marker(L, object_marker); }

	//* Add object(t) and array(t) to the table at <module_idx>: mark t (or a new table) and return it.
	//* Also adds null: the JSON-null sentinel (module.null), e.g. { ordered_at = module.null }.
	inline void open_helpers(lua_State* L, int module_idx) {
		module_idx = lua_absindex(L, module_idx);
		lua_pushcfunction(L, detail::lua_object);
		lua_setfield(L, module_idx, "object");
		lua_pushcfunction(L, detail::lua_array);
		lua_setfield(L, module_idx, "array");
		push_null(L);
		lua_setfield(L, module_idx, "null");
	}

} // namespace reuse::luajson

#endif
