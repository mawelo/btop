// SPDX-License-Identifier: Apache-2.0
//
// lua_api.cpp -- btop-aix (2026-10-19): Lua API (src/btop_lua.cpp) on a fixed snapshot: sandbox, values,
// process query, signals, memory and time limit, JSON bridge (libs/reuse/lua_json_bridge.hpp).

#include <cerrno>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "lua.h"
#include "lauxlib.h"

#include "lua_json_bridge.hpp"

#include "btop_core.hpp"
#include "btop_lua.hpp"

namespace {

	auto make_proc(size_t pid, std::string name, uint64_t mem, double cpu) -> Proc::proc_info {
		Proc::proc_info p;
		p.pid = pid;
		p.name = std::move(name);
		p.cmd = "/usr/bin/" + p.name;
		p.user = "tester";
		p.mem = mem;
		p.cpu_p = cpu;
		p.threads = 1;
		p.state = 'R';
		return p;
	}

	auto fixed_snapshot() -> std::shared_ptr<const Core::Snapshot> {
		auto s = std::make_shared<Core::Snapshot>();
		s->seq = 7;
		s->time_ms = 1000;
		s->cpu = {"POWER9", 42.5, {40.0, 45.0}, {1.0, 2.0, 3.0}, 2};
		s->mem.total = 8u << 30;
		s->mem.used = 3u << 30;
		s->disks.push_back({"/", "hd4", "jfs2", 100, 60, 40, 60});
		s->net.push_back({"en0", "10.0.0.1", "", true, 10, 20, 1000, 2000});
		s->procs = {make_proc(1, "init", 100, 0.1), make_proc(200, "db2sysc", 5000, 30.0), make_proc(300, "sshd", 300, 1.0)};
		return s;
	}

	struct Fixture : ::testing::Test {
		pid_t sent_pid = 0;
		int sent_sig = -1;
		int fail_errno = 0;

		auto options(bool allow_signals = false) -> Lua::Options {
			return {
				.snapshot = [] { return fixed_snapshot(); },
				.send_signal = [this](pid_t pid, int sig) -> std::expected<void, int> {
					sent_pid = pid;
					sent_sig = sig;
					if (fail_errno != 0) return std::unexpected(fail_errno);
					return {};
				},
				.allow_signals = allow_signals,
			};
		}

		//? Run code, return the first result converted to JSON, or the error text as {"error": ...}
		static auto eval(Lua::State& st, const std::string& code) -> nlohmann::json {
			if (auto r = st.do_string(code); not r) return {{"error", r.error()}};
			auto j = reuse::luajson::to_json(st.get(), -1);
			lua_pop(st.get(), 1);
			if (not j) return {{"convert_error", j.error()}};
			return *j;
		}
	};

} // namespace

TEST_F(Fixture, SandboxHasNoHostAccess) {
	Lua::State st(options());
	EXPECT_EQ(eval(st, "return type(io)..type(os)..type(package)..type(debug)"), "nilnilnilnil");
	EXPECT_EQ(eval(st, "return type(load)..type(dofile)..type(loadfile)..type(string.dump)"), "nilnilnilnil");
	EXPECT_EQ(eval(st, "return type(string.format)..type(table.concat)..type(math.floor)"), "functionfunctionfunction");
	// print must not reach stdout (JSON-RPC channel under --mcp-stdio)
	testing::internal::CaptureStdout();
	EXPECT_EQ(eval(st, "print('to stderr', 1); return true"), true);
	EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
}

TEST_F(Fixture, CpuMemoryDisksNetwork) {
	Lua::State st(options());
	const auto cpu = eval(st, "return btop.cpu()");
	EXPECT_EQ(cpu["name"], "POWER9");
	EXPECT_DOUBLE_EQ(cpu["total_percent"].get<double>(), 42.5);
	EXPECT_EQ(cpu["core_percent"].size(), 2u);
	EXPECT_EQ(cpu["load_avg"][2], 3.0);
	EXPECT_EQ(eval(st, "return btop.memory().used"), 3LL << 30);
	EXPECT_EQ(eval(st, "return btop.disks()[1].mount"), "/");
	EXPECT_EQ(eval(st, "return btop.network()[1].upload_speed"), 20);
	EXPECT_EQ(eval(st, "return btop.snapshot_info().seq"), 7);
}

TEST_F(Fixture, ProcessesSortFilterLimit) {
	Lua::State st(options());
	const auto by_mem = eval(st, "return btop.processes{sort = 'memory', limit = 2}");
	ASSERT_TRUE(by_mem.is_array());
	ASSERT_EQ(by_mem.size(), 2u);
	EXPECT_EQ(by_mem[0]["name"], "db2sysc");
	EXPECT_EQ(by_mem[1]["name"], "sshd");
	EXPECT_EQ(eval(st, "return #btop.processes{filter = 'ssh'}"), 1);
	EXPECT_EQ(eval(st, "return btop.processes{sort = 'pid', reverse = true}[1].pid"), 1);
	const auto none = eval(st, "return btop.processes{filter = 'no such process'}");
	EXPECT_TRUE(none.is_array()) << "an empty list must stay a JSON array";
	EXPECT_TRUE(eval(st, "return btop.processes{sort = 'bogus'}").contains("error"));
	EXPECT_TRUE(eval(st, "return btop.processes{limit = -1}").contains("error"));
}

TEST_F(Fixture, NoSnapshotIsAnError) {
	auto o = options();
	o.snapshot = [] { return std::shared_ptr<const Core::Snapshot>{}; };
	Lua::State st(o);
	const auto r = eval(st, "return btop.cpu()");
	ASSERT_TRUE(r.contains("error"));
	EXPECT_NE(r["error"].get<std::string>().find("no measurement"), std::string::npos);
}

TEST_F(Fixture, SignalsDisabledByDefault) {
	Lua::State st(options(false));
	EXPECT_EQ(eval(st, "return btop.signals_allowed"), false);
	EXPECT_TRUE(eval(st, "return btop.send_signal(300, 15)").contains("error"));
	EXPECT_EQ(sent_sig, -1);
}

TEST_F(Fixture, SignalsWhenAllowed) {
	Lua::State st(options(true));
	EXPECT_EQ(eval(st, "return btop.send_signal(300, 15)"), true);
	EXPECT_EQ(sent_pid, 300);
	EXPECT_EQ(sent_sig, 15);
	fail_errno = EPERM;
	EXPECT_EQ(eval(st, "local ok, msg, e = btop.send_signal(1, 9); return {ok == nil, e}"), nlohmann::json::array({true, EPERM}));
	EXPECT_TRUE(eval(st, "return btop.send_signal(0, 9)").contains("error"));
}

TEST_F(Fixture, CoreSendSignalRejectsBadInput) {
	EXPECT_EQ(Core::send_signal(0, 15).error(), ESRCH);
	EXPECT_EQ(Core::send_signal(1, 65).error(), EINVAL);
	EXPECT_TRUE(Core::send_signal(getpid(), 0).has_value()) << "signal 0 only checks existence";
}

TEST_F(Fixture, MemoryLimit) {
	auto o = options();
	o.memory_limit = 4u << 20;
	Lua::State st(o);
	const auto r = eval(st, "return string.rep('x', 16 * 1024 * 1024)");
	ASSERT_TRUE(r.contains("error"));
	EXPECT_NE(r["error"].get<std::string>().find("memory"), std::string::npos);
	EXPECT_EQ(eval(st, "return 1 + 1"), 2) << "state stays usable after a memory error";
}

TEST_F(Fixture, TimeLimit) {
	auto o = options();
	o.call_limit = std::chrono::milliseconds(100);
	Lua::State st(o);
	const auto r = eval(st, "while true do end");
	ASSERT_TRUE(r.contains("error"));
	EXPECT_NE(r["error"].get<std::string>().find("time limit"), std::string::npos);
	EXPECT_EQ(eval(st, "return 'still alive'"), "still alive");
}

TEST_F(Fixture, JsonBridgeRoundTrip) {
	Lua::State st(options());
	lua_State* L = st.get();
	const nlohmann::json in = {{"a", nlohmann::json::array()}, {"o", nlohmann::json::object()}, {"n", 1.5}, {"i", -3}, {"s", "x"}, {"b", false}};
	ASSERT_TRUE(reuse::luajson::push(L, in));
	const auto out = reuse::luajson::to_json(L, -1);
	lua_pop(L, 1);
	ASSERT_TRUE(out.has_value());
	EXPECT_EQ(*out, in);
	EXPECT_EQ(eval(st, "return btop.object{}"), nlohmann::json::object());
	EXPECT_EQ(eval(st, "return btop.array{}"), nlohmann::json::array());
	EXPECT_EQ(eval(st, "return {10, 20}"), nlohmann::json::array({10, 20}));
	EXPECT_TRUE(eval(st, "return 0/0").contains("convert_error"));
	EXPECT_TRUE(eval(st, "local t = {}; t.self = t; return t").contains("convert_error"));
	EXPECT_TRUE(eval(st, "return {1, 2, x = 3}").contains("convert_error"));
	EXPECT_EQ(lua_gettop(L), 0) << "bridge must leave the stack balanced";
}
