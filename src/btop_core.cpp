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
What    : Implementation of the core API: collect_snapshot(), Core::Loop (collector thread with a command queue),
          query_processes() and send_signal().
Why     : The TUI, the Lua binding and the MCP adapter share one UI-free core (see btop_core.hpp).
Deps    : btop_core.hpp, btop_tools.hpp, the collectors of the platform layer (btop_shared.hpp)
Usage   : see btop_core.hpp; design: docs/btop_core.md
History : git log -- src/btop_core.cpp
*/

#include <cerrno>
#include <csignal>
#include <stdexcept>

#include "btop_core.hpp"
#include "btop_tools.hpp"

namespace Core {

	namespace {
		auto last_or_zero(const std::deque<long long>& d) -> double {
			return d.empty() ? 0.0 : static_cast<double>(d.back());
		}
	}

	auto collect_snapshot(uint64_t seq) -> Snapshot {
		Snapshot snap;
		snap.seq = seq;
		snap.time_ms = Tools::time_ms();

		const auto& cpu = Cpu::collect();
		snap.cpu.name = Cpu::cpuName;
		snap.cpu.total_percent = last_or_zero(cpu.cpu_percent.at("total"));
		snap.cpu.core_percent.reserve(cpu.core_percent.size());
		for (const auto& core : cpu.core_percent) snap.cpu.core_percent.push_back(last_or_zero(core));
		snap.cpu.load_avg = cpu.load_avg;
		snap.cpu.core_count = Shared::coreCount;

		const auto& mem = Mem::collect();
		snap.mem.total = Mem::get_totalMem();
		snap.mem.used = mem.stats.at("used");
		snap.mem.available = mem.stats.at("available");
		snap.mem.cached = mem.stats.at("cached");
		snap.mem.free = mem.stats.at("free");
		snap.mem.swap_total = mem.stats.at("swap_total");
		snap.mem.swap_used = mem.stats.at("swap_used");
		snap.mem.swap_free = mem.stats.at("swap_free");
		for (const auto& mount : mem.disks_order) {
			const auto it = mem.disks.find(mount);
			if (it == mem.disks.end()) continue;
			const auto& d = it->second;
			snap.disks.push_back({mount, d.name, d.fstype, d.total, d.used, d.free, d.used_percent});
		}

		Net::collect();
		for (const auto& [iface, n] : Net::current_net) {
			snap.net.push_back({iface, n.ipv4, n.ipv6, n.connected,
				n.stat.at("download").speed, n.stat.at("upload").speed,
				n.stat.at("download").total, n.stat.at("upload").total});
		}

		snap.procs = Proc::collect();
		return snap;
	}

	auto query_processes(const Snapshot& snap, const ProcQuery& query) -> std::expected<std::vector<Proc::proc_info>, std::string> {
		if (Tools::v_index(Proc::sort_vector, query.sort) >= Proc::sort_vector.size())
			return std::unexpected("unknown sort key: " + query.sort);
		std::vector<Proc::proc_info> out;
		out.reserve(snap.procs.size());
		for (const auto& p : snap.procs) {
			if (query.filter.empty() or Proc::matches_filter(p, query.filter)) out.push_back(p);
		}
		Proc::proc_sorter(out, query.sort, query.reverse);
		if (query.limit > 0 and out.size() > query.limit) out.resize(query.limit);
		return out;
	}

	auto send_signal(pid_t pid, int sig) -> std::expected<void, int> {
		if (pid < 1) return std::unexpected(ESRCH);
		if (sig < 0 or sig > 64) return std::unexpected(EINVAL);
		if (kill(pid, sig) != 0) return std::unexpected(errno);
		return {};
	}

	Loop::Loop(uint64_t interval_ms, Collector collector)
	: interval_ms(interval_ms), collector(std::move(collector)) {}

	Loop::~Loop() { stop(); }

	void Loop::start() {
		std::lock_guard lock(mtx);
		if (worker.joinable()) return;
		stopping = false;
		worker = std::thread([this] { run(); });
	}

	void Loop::stop() {
		{
			std::lock_guard lock(mtx);
			stopping = true;
		}
		cv.notify_all();
		if (worker.joinable()) worker.join();
	}

	auto Loop::snapshot() const -> std::shared_ptr<const Snapshot> {
		std::lock_guard lock(mtx);
		return current;
	}

	auto Loop::wait_for_seq(uint64_t seq, uint64_t timeout_ms) const -> bool {
		std::unique_lock lock(mtx);
		return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return current and current->seq >= seq; });
	}

	void Loop::post(std::function<void()> cmd) {
		{
			std::lock_guard lock(mtx);
			//? A command posted after stop() would never run and its caller would wait forever
			if (stopping or not worker.joinable()) throw std::runtime_error("core loop is not running");
			commands.push_back(std::move(cmd));
		}
		cv.notify_all();
	}

	void Loop::run() {
		uint64_t seq = 0;
		auto next = std::chrono::steady_clock::now();
		while (true) {
			std::function<void()> cmd;
			{
				std::unique_lock lock(mtx);
				cv.wait_until(lock, next, [&] { return stopping or not commands.empty(); });
				if (stopping) break;
				if (not commands.empty()) {
					cmd = std::move(commands.front());
					commands.pop_front();
				}
			}
			if (cmd) {
				cmd();	//? packaged_task stores exceptions in its future
				continue;
			}
			auto snap = std::make_shared<const Snapshot>(collector(++seq));
			{
				std::lock_guard lock(mtx);
				current = std::move(snap);
			}
			cv.notify_all();
			next += std::chrono::milliseconds(interval_ms);
			if (const auto now = std::chrono::steady_clock::now(); next < now) next = now;
		}
		//? Run commands that arrived before stop(): their callers wait on the result
		std::deque<std::function<void()>> rest;
		{
			std::lock_guard lock(mtx);
			rest.swap(commands);
		}
		for (auto& c : rest) c();
	}

} // namespace Core
