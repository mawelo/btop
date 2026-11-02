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
What    : Lua 5.4 binding of the core API: module "btop" in a reuse::lua::Sandbox (libs/reuse/lua_sandbox.hpp:
          no io/os/package/debug, no dofile/loadfile, memory limit by allocator, time limit per call by
          instruction hook, print() to stderr). Since 2026-10-31 the sandbox comes from a shared header
          instead of an own copy; the module functions find their State through an upvalue, because the
          sandbox owns the state's extra space.
Why     : Lua is the canonical API of btop (single source of truth);
          adapters (MCP) reach the core only through it. API reference: docs/lua-api.md
Deps    : libs/lua-5.4.8 compiled as C++ (errors unwind C++ frames), libs/reuse/lua_sandbox.hpp, btop_core.hpp
Usage   : Lua::State L({snapshot_fn, signal_fn, allow_signals});
          L.do_string("return btop.cpu().total_percent");   // or load a script with L.do_file(path)
History : git log -- src/btop_lua.hpp
*/

#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <sys/types.h>

#include "lua_sandbox.hpp"

#include "btop_core.hpp"

namespace Lua {

	struct Options {
		std::function<std::shared_ptr<const Core::Snapshot>()> snapshot;	//? current snapshot or nullptr
		std::function<std::expected<void, int>(pid_t, int)> send_signal;	//? runs Core::send_signal (on the core thread)
		bool allow_signals = false;		//? btop.send_signal raises an error unless set
		size_t memory_limit = 64u << 20;	//? bytes for the whole state
		std::chrono::milliseconds call_limit{2000};	//? per protected call (guard())
	};

	class State {
	public:
		explicit State(Options opts);
		~State();
		State(const State&) = delete;
		State& operator=(const State&) = delete;

		[[nodiscard]] auto get() const noexcept -> lua_State* { return sb.get(); }
		[[nodiscard]] auto options() const noexcept -> const Options& { return opts; }
		//* The sandbox itself, e.g. for reuse::mcp::LuaTools (tool registry in the same state)
		[[nodiscard]] auto sandbox() noexcept -> reuse::lua::Sandbox& { return sb; }

		//* Run a chunk; on success its first result stays on the stack (or nil). Error text otherwise, stack unchanged.
		auto do_string(const std::string& code, const std::string& chunkname = "=string") -> std::expected<void, std::string> {
			return sb.do_string(code, chunkname);
		}
		auto do_file(const std::filesystem::path& file) -> std::expected<void, std::string> { return sb.do_file(file); }

		//* lua_pcall with the time limit armed; on error the message replaces the function and its arguments.
		auto pcall(int nargs, int nresults) -> std::expected<void, std::string> { return sb.pcall(nargs, nresults); }

	private:
		Options opts;			//? before sb: sb is built from opts.memory_limit / opts.call_limit
		reuse::lua::Sandbox sb;
	};

	//* "<version>+<commit>" as btop --version prints it
	auto version() -> std::string;

} // namespace Lua
