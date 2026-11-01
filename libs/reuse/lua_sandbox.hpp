// __REUSABLE__
//
// lua_sandbox.hpp - sandboxed Lua 5.4 state with memory limit and per-call time limit.
//
// Who:     Martin Lonkwitz
// What:    reuse::lua::Sandbox owns a lua_State with only harmless libraries (base, string, table, math,
//          utf8, coroutine), without dofile/loadfile/load/string.dump, text chunks only, a byte limit for the
//          whole state (allocator) and a time limit per protected call (instruction-count hook). print()
//          goes to a caller-supplied sink, never to stdout (stdout may be a protocol channel, e.g. MCP stdio).
// When:    2026-10-19
// Why:     Lua is the canonical API of an app; scripts (MCP tool
//          registries, provider plugins) run without io/os/package/debug and within time and memory limits.
//          Generalised from btop-aix src/btop_lua.cpp (same limits and library set).
// Project: reusable; used by btop-aix (src/btop_lua.cpp) and further projects
// Dependencies: Lua 5.4 COMPILED AS C++ (e.g. lib/c/lua-5.4.8, all *.c with LANGUAGE CXX): lua_error then
//          throws and unwinds C++ frames instead of longjmp over their destructors. With a C-compiled Lua,
//          C functions called from Lua must not hold objects with destructors when they raise an error.
//          C++23 (std::expected).
//
// Usage:
//     reuse::lua::Sandbox sb({.memory_limit = 128u << 20, .call_limit = std::chrono::seconds(30),
//                             .print = [](std::string_view s) { std::cerr << s << '\n'; }});
//     lua_State* L = sb.get();
//     luaL_newlib(L, my_funcs); lua_setglobal(L, "app");          // bind the app API
//     if (auto r = sb.do_file("tools.lua"); !r) ...;              // result (1 value) stays on the stack
//     ... push function + args ...; auto r = sb.pcall(nargs, 1);  // time limit armed
//
// Correctness: The allocator counts the bytes Lua holds (osize is the old size only for ptr != nullptr);
//     a request above the limit returns nullptr and Lua raises a memory error (state stays usable).
//     The hook runs every hook_count instructions; C functions are not interrupted (their own runtime
//     counts against the deadline only once control returns to Lua).
// Security: Light userdata, the registry and metatables of strings stay reachable as in plain Lua; no
//     function of the sandbox reaches files, processes or the environment. Chunks are loaded in mode "t"
//     (binary chunks can crash the VM). Not thread-safe: one state per thread.
// History: git log -- cpp/lua_sandbox.hpp

#ifndef REUSE_LUA_SANDBOX_HPP
#define REUSE_LUA_SANDBOX_HPP

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace reuse::lua {

	struct Options {
		size_t memory_limit = 64u << 20;	//? bytes for the whole state
		std::chrono::milliseconds call_limit{2000};	//? per pcall()/do_file()/do_string()
		std::function<void(std::string_view)> print;	//? sink for print(); empty = discard
	};

	class Sandbox {
	public:
		static constexpr int hook_count = 10000;	//? instructions between deadline checks

		explicit Sandbox(Options o) : opts(std::move(o)) {
			L = lua_newstate(&Sandbox::alloc, this);
			if (L == nullptr) throw std::runtime_error("lua_newstate failed (memory limit)");
			*static_cast<Sandbox**>(lua_getextraspace(L)) = this;
			open_libs();
		}
		~Sandbox() {
			if (L != nullptr) lua_close(L);
		}
		Sandbox(const Sandbox&) = delete;
		Sandbox& operator=(const Sandbox&) = delete;

		[[nodiscard]] auto get() const noexcept -> lua_State* { return L; }
		[[nodiscard]] auto options() const noexcept -> const Options& { return opts; }
		[[nodiscard]] auto memory_used() const noexcept -> size_t { return used; }

		//* lua_pcall with the time limit armed; on error the message replaces the function and its arguments.
		auto pcall(int nargs, int nresults) -> std::expected<void, std::string> {
			deadline = std::chrono::steady_clock::now() + opts.call_limit;
			lua_sethook(L, &Sandbox::hook, LUA_MASKCOUNT, hook_count);
			const int rc = lua_pcall(L, nargs, nresults, 0);
			lua_sethook(L, nullptr, 0, 0);
			if (rc == LUA_OK) return {};
			return std::unexpected(pop_error());
		}

		//* Run a chunk; on success its first result stays on the stack (or nil).
		auto do_string(const std::string& code, const std::string& chunkname = "=string") -> std::expected<void, std::string> {
			if (luaL_loadbufferx(L, code.data(), code.size(), chunkname.c_str(), "t") != LUA_OK) return std::unexpected(pop_error());
			return pcall(0, 1);
		}
		auto do_file(const std::filesystem::path& file) -> std::expected<void, std::string> {
			if (luaL_loadfilex(L, file.c_str(), "t") != LUA_OK) return std::unexpected(pop_error());
			return pcall(0, 1);
		}

	private:
		static auto self(lua_State* L) -> Sandbox& { return **static_cast<Sandbox**>(lua_getextraspace(L)); }

		auto pop_error() -> std::string {
			size_t len = 0;
			const char* msg = lua_tolstring(L, -1, &len);
			std::string err = msg != nullptr ? std::string(msg, len) : std::string("error object is not a string");
			lua_pop(L, 1);
			return err;
		}

		static auto alloc(void* ud, void* ptr, size_t osize, size_t nsize) -> void* {
			auto& s = *static_cast<Sandbox*>(ud);
			const size_t old_size = ptr != nullptr ? osize : 0;	//? otherwise osize encodes the object type
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

		static void hook(lua_State* L, lua_Debug* /* ar */) {
			if (std::chrono::steady_clock::now() > self(L).deadline) luaL_error(L, "time limit exceeded");
		}

		//? print(...) like plain Lua (tostring, tab-separated), but into the sink
		static auto l_print(lua_State* L) -> int {
			const int n = lua_gettop(L);
			std::string line;
			for (int i = 1; i <= n; ++i) {
				size_t len = 0;
				const char* s = luaL_tolstring(L, i, &len);
				if (i > 1) line += '\t';
				line.append(s, len);
				lua_pop(L, 1);
			}
			if (const auto& sink = self(L).opts.print) sink(line);
			return 0;
		}

		void open_libs() {
			static constexpr luaL_Reg libs[] = {
				{LUA_GNAME, luaopen_base},	   {LUA_STRLIBNAME, luaopen_string}, {LUA_TABLIBNAME, luaopen_table},
				{LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8},	 {LUA_COLIBNAME, luaopen_coroutine},
			};
			for (const auto& lib : libs) {
				luaL_requiref(L, lib.name, lib.func, 1);
				lua_pop(L, 1);
			}
			for (const char* name : {"dofile", "loadfile", "load"}) {	//? files and (binary) chunks
				lua_pushnil(L);
				lua_setglobal(L, name);
			}
			lua_pushcfunction(L, &Sandbox::l_print);
			lua_setglobal(L, "print");
			lua_getglobal(L, LUA_STRLIBNAME);
			lua_pushnil(L);
			lua_setfield(L, -2, "dump");
			lua_pop(L, 1);
		}

		Options opts;
		size_t used = 0;
		std::chrono::steady_clock::time_point deadline{};
		lua_State* L = nullptr;
	};

} // namespace reuse::lua

#endif
