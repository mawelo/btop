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

Project : btop-aix (branch aix), 2026-10-19. See btop_lua.hpp; API reference docs/lua-api.md (keep in sync).
History : git log -- src/btop_lua.cpp
*/

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

//? Lua is compiled as C++ (CMakeLists.txt), so its headers are included without extern "C"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lua_json_bridge.hpp"

#include "btop_lua.hpp"
#include "btop_shared.hpp"
#include "config.h"

namespace Lua {

	namespace {
		constexpr int hook_count = 10000;	//? instructions between deadline checks

		auto self(lua_State* L) -> State& {
			return **static_cast<State**>(lua_getextraspace(L));
		}

		auto current(lua_State* L) -> std::shared_ptr<const Core::Snapshot> {
			auto snap = self(L).options().snapshot ? self(L).options().snapshot() : nullptr;
			if (not snap) luaL_error(L, "no measurement yet (the first collection runs at start)");
			return snap;
		}

		void set_int(lua_State* L, const char* key, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, key); }
		void set_num(lua_State* L, const char* key, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, key); }
		void set_str(lua_State* L, const char* key, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, key); }
		void set_bool(lua_State* L, const char* key, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, key); }

		auto to_int(uint64_t v) -> lua_Integer {
			return v > static_cast<uint64_t>(LUA_MAXINTEGER) ? LUA_MAXINTEGER : static_cast<lua_Integer>(v);
		}

		auto l_version(lua_State* L) -> int {
			const auto v = version();
			lua_pushlstring(L, v.data(), v.size());
			return 1;
		}

		auto l_snapshot_info(lua_State* L) -> int {
			const auto snap = current(L);
			lua_createtable(L, 0, 2);
			set_int(L, "seq", to_int(snap->seq));
			set_int(L, "time_ms", to_int(snap->time_ms));
			return 1;
		}

		auto l_cpu(lua_State* L) -> int {
			const auto snap = current(L);
			const auto& c = snap->cpu;
			lua_createtable(L, 0, 5);
			set_str(L, "name", c.name);
			set_num(L, "total_percent", c.total_percent);
			set_int(L, "core_count", c.core_count);
			lua_createtable(L, static_cast<int>(c.core_percent.size()), 0);
			for (size_t i = 0; i < c.core_percent.size(); ++i) {
				lua_pushnumber(L, c.core_percent[i]);
				lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
			}
			reuse::luajson::mark_array(L);
			lua_setfield(L, -2, "core_percent");
			lua_createtable(L, 3, 0);
			for (int i = 0; i < 3; ++i) {
				lua_pushnumber(L, c.load_avg[static_cast<size_t>(i)]);
				lua_rawseti(L, -2, i + 1);
			}
			reuse::luajson::mark_array(L);
			lua_setfield(L, -2, "load_avg");
			return 1;
		}

		auto l_memory(lua_State* L) -> int {
			const auto snap = current(L);
			const auto& m = snap->mem;
			lua_createtable(L, 0, 8);
			set_int(L, "total", to_int(m.total));
			set_int(L, "used", to_int(m.used));
			set_int(L, "available", to_int(m.available));
			set_int(L, "cached", to_int(m.cached));
			set_int(L, "free", to_int(m.free));
			set_int(L, "swap_total", to_int(m.swap_total));
			set_int(L, "swap_used", to_int(m.swap_used));
			set_int(L, "swap_free", to_int(m.swap_free));
			return 1;
		}

		auto l_disks(lua_State* L) -> int {
			const auto snap = current(L);
			lua_createtable(L, static_cast<int>(snap->disks.size()), 0);
			lua_Integer i = 1;
			for (const auto& d : snap->disks) {
				lua_createtable(L, 0, 7);
				set_str(L, "mount", d.mount);
				set_str(L, "name", d.name);
				set_str(L, "fstype", d.fstype);
				set_int(L, "total", d.total);
				set_int(L, "used", d.used);
				set_int(L, "free", d.free);
				set_int(L, "used_percent", d.used_percent);
				lua_rawseti(L, -2, i++);
			}
			reuse::luajson::mark_array(L);
			return 1;
		}

		auto l_network(lua_State* L) -> int {
			const auto snap = current(L);
			lua_createtable(L, static_cast<int>(snap->net.size()), 0);
			lua_Integer i = 1;
			for (const auto& n : snap->net) {
				lua_createtable(L, 0, 8);
				set_str(L, "iface", n.iface);
				set_str(L, "ipv4", n.ipv4);
				set_str(L, "ipv6", n.ipv6);
				set_bool(L, "connected", n.connected);
				set_int(L, "download_speed", to_int(n.download_speed));
				set_int(L, "upload_speed", to_int(n.upload_speed));
				set_int(L, "download_total", to_int(n.download_total));
				set_int(L, "upload_total", to_int(n.upload_total));
				lua_rawseti(L, -2, i++);
			}
			reuse::luajson::mark_array(L);
			return 1;
		}

		auto opt_field_string(lua_State* L, const char* key, std::string def) -> std::string {
			lua_getfield(L, 1, key);
			if (not lua_isnil(L, -1)) {
				if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "processes: field '%s' must be a string", key);
				size_t len = 0;
				const char* s = lua_tolstring(L, -1, &len);
				def.assign(s, len);
			}
			lua_pop(L, 1);
			return def;
		}

		auto l_processes(lua_State* L) -> int {
			Core::ProcQuery q;
			if (not lua_isnoneornil(L, 1)) {
				luaL_checktype(L, 1, LUA_TTABLE);
				q.sort = opt_field_string(L, "sort", q.sort);
				q.filter = opt_field_string(L, "filter", q.filter);
				lua_getfield(L, 1, "reverse");
				if (not lua_isnil(L, -1)) {
					if (not lua_isboolean(L, -1)) luaL_error(L, "processes: field 'reverse' must be a boolean");
					q.reverse = lua_toboolean(L, -1);
				}
				lua_pop(L, 1);
				lua_getfield(L, 1, "limit");
				if (not lua_isnil(L, -1)) {
					if (not lua_isinteger(L, -1) or lua_tointeger(L, -1) < 0) luaL_error(L, "processes: field 'limit' must be an integer >= 0");
					q.limit = static_cast<size_t>(lua_tointeger(L, -1));
				}
				lua_pop(L, 1);
			}
			const auto snap = current(L);
			auto procs = Core::query_processes(*snap, q);
			if (not procs) luaL_error(L, "processes: %s", procs.error().c_str());
			lua_createtable(L, static_cast<int>(procs->size()), 0);
			lua_Integer i = 1;
			for (const auto& p : *procs) {
				lua_createtable(L, 0, 9);
				set_int(L, "pid", to_int(p.pid));
				set_int(L, "ppid", to_int(p.ppid));
				set_str(L, "name", p.name);
				set_str(L, "cmd", p.cmd);
				set_str(L, "user", p.user);
				set_num(L, "cpu_percent", p.cpu_p);
				set_int(L, "mem_bytes", to_int(p.mem));
				set_int(L, "threads", to_int(p.threads));
				set_str(L, "state", std::string(1, p.state));
				lua_rawseti(L, -2, i++);
			}
			reuse::luajson::mark_array(L);
			return 1;
		}

		auto l_sort_keys(lua_State* L) -> int {
			lua_createtable(L, static_cast<int>(Proc::sort_vector.size()), 0);
			lua_Integer i = 1;
			for (const auto& k : Proc::sort_vector) {
				lua_pushlstring(L, k.data(), k.size());
				lua_rawseti(L, -2, i++);
			}
			reuse::luajson::mark_array(L);
			return 1;
		}

		auto l_send_signal(lua_State* L) -> int {
			const lua_Integer pid = luaL_checkinteger(L, 1);
			const lua_Integer sig = luaL_checkinteger(L, 2);
			const auto& o = self(L).options();
			if (not o.allow_signals) luaL_error(L, "send_signal: disabled (start btop with --mcp-allow-signals)");
			if (not o.send_signal) luaL_error(L, "send_signal: no core attached");
			if (pid < 1 or pid > std::numeric_limits<pid_t>::max() or sig < 0 or sig > 64)
				luaL_error(L, "send_signal: pid must be >= 1, signal 0..64");
			const auto r = o.send_signal(static_cast<pid_t>(pid), static_cast<int>(sig));
			if (r) {
				lua_pushboolean(L, 1);
				return 1;
			}
			lua_pushnil(L);
			lua_pushstring(L, std::strerror(r.error()));
			lua_pushinteger(L, r.error());
			return 3;
		}

		//? print() of the base library writes to stdout, which carries JSON-RPC under --mcp-stdio: send it to stderr
		auto l_print(lua_State* L) -> int {
			const int n = lua_gettop(L);
			std::string line;
			for (int i = 1; i <= n; ++i) {
				size_t len = 0;
				const char* s = luaL_tolstring(L, i, &len);
				if (i > 1) line += '\t';
				line.append(s, len);
				lua_pop(L, 1);
			}
			line += '\n';
			std::fwrite(line.data(), 1, line.size(), stderr);
			return 0;
		}

		constexpr luaL_Reg btop_funcs[] = {
			{"version", l_version},
			{"snapshot_info", l_snapshot_info},
			{"cpu", l_cpu},
			{"memory", l_memory},
			{"disks", l_disks},
			{"network", l_network},
			{"processes", l_processes},
			{"sort_keys", l_sort_keys},
			{"send_signal", l_send_signal},
			{nullptr, nullptr},
		};

		void open_sandbox(lua_State* L) {
			//? Only libraries without access to files, processes or the host: no io, os, package, debug
			static constexpr luaL_Reg libs[] = {
				{LUA_GNAME, luaopen_base},
				{LUA_STRLIBNAME, luaopen_string},
				{LUA_TABLIBNAME, luaopen_table},
				{LUA_MATHLIBNAME, luaopen_math},
				{LUA_UTF8LIBNAME, luaopen_utf8},
				{LUA_COLIBNAME, luaopen_coroutine},
			};
			for (const auto& lib : libs) {
				luaL_requiref(L, lib.name, lib.func, 1);
				lua_pop(L, 1);
			}
			//? File access and bytecode: loading binary chunks can crash the VM
			for (const char* name : {"dofile", "loadfile", "load"}) {
				lua_pushnil(L);
				lua_setglobal(L, name);
			}
			lua_pushcfunction(L, l_print);
			lua_setglobal(L, "print");
			lua_getglobal(L, LUA_STRLIBNAME);
			lua_pushnil(L);
			lua_setfield(L, -2, "dump");
			lua_pop(L, 1);
		}
	} // namespace

	auto version() -> std::string {
		return GIT_COMMIT.empty() ? Global::Version : Global::Version + "+" + std::string(GIT_COMMIT);
	}

	State::State(Options o) : opts(std::move(o)) {
		L = lua_newstate(&State::alloc, this);
		if (L == nullptr) throw std::runtime_error("lua_newstate failed (memory limit)");
		*static_cast<State**>(lua_getextraspace(L)) = this;
		open_sandbox(L);
		luaL_newlib(L, btop_funcs);
		reuse::luajson::open_helpers(L, -1);
		lua_pushboolean(L, opts.allow_signals);
		lua_setfield(L, -2, "signals_allowed");
		lua_setglobal(L, "btop");
	}

	State::~State() {
		if (L != nullptr) lua_close(L);
	}

	auto State::alloc(void* ud, void* ptr, size_t osize, size_t nsize) -> void* {
		auto& s = *static_cast<State*>(ud);
		//? osize is the old block size only when ptr != nullptr (otherwise it encodes the object type)
		const size_t old_size = ptr != nullptr ? osize : 0;
		if (nsize == 0) {
			s.used -= old_size;
			std::free(ptr);
			return nullptr;
		}
		if (nsize > old_size and s.used - old_size + nsize > s.opts.memory_limit) return nullptr;	//? Lua raises a memory error
		void* p = std::realloc(ptr, nsize);
		if (p != nullptr) s.used = s.used - old_size + nsize;
		return p;
	}

	void State::hook(lua_State* L, lua_Debug* /* ar */) {
		if (std::chrono::steady_clock::now() > self(L).deadline) luaL_error(L, "time limit exceeded");
	}

	auto State::pcall(int nargs, int nresults) -> std::expected<void, std::string> {
		deadline = std::chrono::steady_clock::now() + opts.call_limit;
		lua_sethook(L, &State::hook, LUA_MASKCOUNT, hook_count);
		const int rc = lua_pcall(L, nargs, nresults, 0);
		lua_sethook(L, nullptr, 0, 0);
		if (rc == LUA_OK) return {};
		size_t len = 0;
		const char* msg = lua_tolstring(L, -1, &len);
		std::string err = msg != nullptr ? std::string(msg, len) : std::string("error object is not a string");
		lua_pop(L, 1);
		return std::unexpected(std::move(err));
	}

	auto State::do_string(const std::string& code, const std::string& chunkname) -> std::expected<void, std::string> {
		//? mode "t": text only, never precompiled bytecode
		if (luaL_loadbufferx(L, code.data(), code.size(), chunkname.c_str(), "t") != LUA_OK) {
			std::string err = lua_tostring(L, -1);
			lua_pop(L, 1);
			return std::unexpected(std::move(err));
		}
		return pcall(0, 1);
	}

	auto State::do_file(const std::filesystem::path& file) -> std::expected<void, std::string> {
		if (luaL_loadfilex(L, file.c_str(), "t") != LUA_OK) {
			std::string err = lua_tostring(L, -1);
			lua_pop(L, 1);
			return std::unexpected(std::move(err));
		}
		return pcall(0, 1);
	}

} // namespace Lua
