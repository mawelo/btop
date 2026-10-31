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
What    : UI-free core API: measurements as immutable snapshots (queries) and process signals (command).
          Core::Loop owns the collectors on one thread; other threads only read snapshots or post commands.
Why     : Code rule "App-Architektur: Scripting-API & MCP": core API -> Lua binding -> adapters (TUI, MCP);
          no feature exists only in the UI (sending signals lived in btop_menu.cpp before).
Deps    : btop_shared.hpp (collectors), btop_tools.hpp. Docs: docs/btop_core.md
History : git log -- src/btop_core.hpp
*/

#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

#include "btop_shared.hpp"

namespace Core {

	struct CpuSample {
		std::string name;
		double total_percent{};
		std::vector<double> core_percent;
		std::array<double, 3> load_avg{};
		long core_count{};
	};

	struct MemSample {
		uint64_t total{}, used{}, available{}, cached{}, free{};
		uint64_t swap_total{}, swap_used{}, swap_free{};
	};

	struct DiskSample {
		std::string mount, name, fstype;
		int64_t total{}, used{}, free{};
		int used_percent{};
	};

	struct NetSample {
		std::string iface, ipv4, ipv6;
		bool connected{};
		uint64_t download_speed{}, upload_speed{};	//? bytes per second
		uint64_t download_total{}, upload_total{};	//? bytes since start of btop
	};

	struct Snapshot {
		uint64_t seq{};			//? 1 for the first collection, +1 per cycle
		uint64_t time_ms{};		//? epoch milliseconds of the collection
		CpuSample cpu;
		MemSample mem;
		std::vector<DiskSample> disks;
		std::vector<NetSample> net;
		std::vector<Proc::proc_info> procs;
	};

	struct ProcQuery {
		std::string sort = "cpu lazy";	//? one of Proc::sort_vector
		bool reverse = false;
		std::string filter;				//? same syntax as the process filter of the TUI ("!" prefix: regex)
		size_t limit = 0;				//? 0 = all
	};

	//* Run all collectors once and copy their results. Not thread-safe: collectors keep static state.
	auto collect_snapshot(uint64_t seq) -> Snapshot;

	//* Sort, filter and limit the processes of a snapshot; O(n log n). Unknown sort key -> unexpected(message).
	auto query_processes(const Snapshot& snap, const ProcQuery& query) -> std::expected<std::vector<Proc::proc_info>, std::string>;

	//* Send signal <sig> to <pid>. Returns errno on failure (ESRCH for pid < 1, EINVAL for an invalid signal).
	auto send_signal(pid_t pid, int sig) -> std::expected<void, int>;

	//* Core event loop: one thread collects every <interval_ms> and runs posted commands in order.
	//* snapshot() is safe from any thread; call() blocks the caller until the command ran on the core thread.
	class Loop {
	public:
		using Collector = std::function<Snapshot(uint64_t seq)>;

		explicit Loop(uint64_t interval_ms, Collector collector = collect_snapshot);
		~Loop();
		Loop(const Loop&) = delete;
		Loop& operator=(const Loop&) = delete;

		void start();
		void stop();

		[[nodiscard]] auto snapshot() const -> std::shared_ptr<const Snapshot>;

		//* Wait until at least <seq> snapshots exist or <timeout_ms> passed; true if reached.
		auto wait_for_seq(uint64_t seq, uint64_t timeout_ms) const -> bool;

		template <typename F>
		auto call(F&& fn) -> decltype(fn()) {
			auto task = std::make_shared<std::packaged_task<decltype(fn())()>>(std::forward<F>(fn));
			auto result = task->get_future();
			post([task] { (*task)(); });
			return result.get();
		}

	private:
		void post(std::function<void()> cmd);
		void run();

		uint64_t interval_ms;
		Collector collector;
		mutable std::mutex mtx;
		mutable std::condition_variable cv;
		std::deque<std::function<void()>> commands;
		std::shared_ptr<const Snapshot> current;
		bool stopping = false;
		std::thread worker;
	};

} // namespace Core
