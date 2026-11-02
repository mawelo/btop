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
What    : Mcp::run_stdio(): headless start of btop as MCP server over stdio (config, Core::Loop, tool registry,
          SIGHUP reload, serving until EOF).
Why     : MCP clients query the measurements of an AIX host without a terminal (see btop_mcp.hpp).
Deps    : btop_mcp.hpp, btop_mcp_tools.hpp, btop_core.hpp, btop_config.hpp, btop_log.hpp, btop_shared.hpp,
          btop_theme.hpp; skeleton libs/reuse/mcp_stdio_server.hpp through btop_mcp_tools.hpp
Usage   : btop --mcp-stdio [--mcp-tools FILE] [--mcp-allow-signals]; design: docs/btop_mcp.md
History : git log -- src/btop_mcp.cpp
*/

#include <atomic>
#include <csignal>
#include <iostream>

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
