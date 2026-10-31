// SPDX-License-Identifier: Apache-2.0
//
// mcp_roundtrip.cpp -- btop-aix (2026-10-19): MCP protocol round trip against the real registry mcp/tools.lua:
// reusable skeleton (libs/reuse/mcp_stdio_server.hpp) + Mcp::LuaTools + Lua API on a fixed snapshot.

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "btop_core.hpp"
#include "btop_lua.hpp"
#include "btop_mcp_tools.hpp"

namespace {

	const std::filesystem::path tools_file = std::filesystem::path(BTOP_SOURCE_DIR) / "mcp" / "tools.lua";

	auto snapshot() -> std::shared_ptr<const Core::Snapshot> {
		auto s = std::make_shared<Core::Snapshot>();
		s->seq = 2;
		Proc::proc_info a, b;
		a.pid = 10; a.name = "alpha"; a.cpu_p = 5.0;
		b.pid = 20; b.name = "beta"; b.cpu_p = 50.0;
		s->procs = {a, b};
		return s;
	}

	auto lua_options(bool allow_signals) -> Lua::Options {
		return {
			.snapshot = [] { return snapshot(); },
			.send_signal = [](pid_t, int) -> std::expected<void, int> { return {}; },
			.allow_signals = allow_signals,
		};
	}

	//? Feed request lines, return responses by id (notifications and id-less errors under key "null")
	auto exchange(reuse::mcp::ToolHost& host, const std::vector<std::string>& lines) -> std::map<std::string, nlohmann::json> {
		std::string input;
		for (const auto& l : lines) input += l + "\n";
		std::istringstream in(input);
		std::ostringstream out;
		reuse::mcp::StdioServer server({"btop", "test", ""}, host, in, out);
		EXPECT_EQ(server.run(), 0);
		std::map<std::string, nlohmann::json> by_id;
		std::istringstream responses(out.str());
		for (std::string line; std::getline(responses, line);) {
			auto j = nlohmann::json::parse(line);
			by_id[j["id"].dump()] = j;
		}
		return by_id;
	}

	auto names(const nlohmann::json& tools) -> std::vector<std::string> {
		std::vector<std::string> n;
		for (const auto& t : tools) n.push_back(t["name"]);
		return n;
	}

} // namespace

TEST(McpRoundTrip, InitializeListCall) {
	Lua::State st(lua_options(false));
	Mcp::LuaTools tools(st, tools_file);
	ASSERT_TRUE(tools.load().has_value());
	auto r = exchange(tools, {
		R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}})",
		R"({"jsonrpc":"2.0","method":"notifications/initialized"})",
		R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})",
		R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"processes","arguments":{"limit":1}}})",
		R"({"jsonrpc":"2.0","id":4,"method":"ping"})",
	});
	EXPECT_EQ(r.size(), 4u) << "the notification gets no answer";
	EXPECT_EQ(r["1"]["result"]["protocolVersion"], "2024-11-05");
	EXPECT_EQ(r["1"]["result"]["serverInfo"]["name"], "btop");
	EXPECT_EQ(names(r["2"]["result"]["tools"]),
		(std::vector<std::string>{"snapshot_info", "cpu", "memory", "disks", "network", "processes"}));
	const auto& call = r["3"]["result"];
	EXPECT_EQ(call["isError"], false);
	ASSERT_EQ(call["structuredContent"]["processes"].size(), 1u);
	EXPECT_EQ(call["structuredContent"]["processes"][0]["name"], "beta") << "default sort 'cpu lazy' puts 50 % first";
	EXPECT_EQ(nlohmann::json::parse(call["content"][0]["text"].get<std::string>()), call["structuredContent"]);
	EXPECT_EQ(r["4"]["result"], nlohmann::json::object());
}

TEST(McpRoundTrip, Errors) {
	Lua::State st(lua_options(false));
	Mcp::LuaTools tools(st, tools_file);
	ASSERT_TRUE(tools.load().has_value());
	auto r = exchange(tools, {
		R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})",
		R"({"jsonrpc":"2.0","id":2,"method":"no/such"})",
		R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"send_signal","arguments":{"pid":1,"signal":9}}})",
		R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"processes","arguments":{"limit":5000}}})",
		R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"cpu","arguments":[]}})",
		R"([{"jsonrpc":"2.0","id":6,"method":"ping"}])",
		R"(not json)",
	});
	EXPECT_EQ(r["1"]["result"]["protocolVersion"], "2025-06-18") << "unknown version -> newest supported";
	EXPECT_EQ(r["2"]["error"]["code"], -32601);
	EXPECT_EQ(r["3"]["error"]["code"], -32602) << "send_signal is not listed without --mcp-allow-signals";
	EXPECT_EQ(r["4"]["result"]["isError"], true);
	EXPECT_EQ(r["5"]["error"]["code"], -32602);
	EXPECT_EQ(r["null"]["error"]["code"], -32700) << "last id-less error: the parse error";
}

TEST(McpRoundTrip, SignalToolOnlyWhenAllowed) {
	Lua::State st(lua_options(true));
	Mcp::LuaTools tools(st, tools_file);
	ASSERT_TRUE(tools.load().has_value());
	auto r = exchange(tools, {
		R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})",
		R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"send_signal","arguments":{"pid":10,"signal":15}}})",
	});
	EXPECT_EQ(names(r["1"]["result"]["tools"]).back(), "send_signal");
	EXPECT_EQ(r["2"]["result"]["structuredContent"]["sent"], true);
}

TEST(McpRoundTrip, ReloadKeepsPreviousRegistryOnError) {
	const auto tmp = std::filesystem::temp_directory_path() / ("btop_mcp_reload_" + std::to_string(getpid()) + ".lua");
	auto write = [&](const std::string& text) { std::ofstream(tmp) << text; };
	write(R"(return { { name = "one", description = "d", inputSchema = btop.object{ type = "object" }, handler = function() return 1 end } })");
	Lua::State st(lua_options(false));
	Mcp::LuaTools tools(st, tmp);
	ASSERT_TRUE(tools.load().has_value());
	write("return { this is not lua");
	EXPECT_FALSE(tools.reload().has_value());
	EXPECT_EQ(names(tools.list_tools()), std::vector<std::string>{"one"});
	write(R"(return { { name = "one", description = "d", inputSchema = btop.object{ type = "object" }, handler = function() return 1 end },
	                  { name = "one", description = "d", inputSchema = btop.object{ type = "object" }, handler = function() return 1 end } })");
	EXPECT_FALSE(tools.reload().has_value()) << "duplicate names are rejected";
	write(R"(return { { name = "two", description = "d", inputSchema = btop.object{ type = "object" }, handler = function(a) return a end } })");
	ASSERT_TRUE(tools.reload().has_value());
	EXPECT_EQ(names(tools.list_tools()), std::vector<std::string>{"two"});
	const auto echo = tools.call_tool("two", {{"k", "v"}});
	ASSERT_TRUE(echo.has_value());
	EXPECT_EQ(*echo, (nlohmann::json{{"k", "v"}}));
	std::filesystem::remove(tmp);
}
