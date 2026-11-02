// __REUSABLE__
//
// mcp_stdio_server.hpp - Model Context Protocol server skeleton for the stdio transport.
//
// Who:     Martin Lonkwitz
// What:    JSON-RPC 2.0 over newline-delimited JSON on an istream/ostream pair (MCP transport "stdio"):
//          initialize handshake, ping, tools/list, tools/call, error objects. The tools themselves come
//          from a ToolHost (in the intended use: a Lua script); the skeleton holds no tool logic.
// When:    2026-10-19
// Why:     A fixed, reusable MCP skeleton stays outside the tool code of the host language, so that a
//          new tool is a script change, not a rebuild.
// Project: reusable, originally btop-aix (btop --mcp-stdio); used by further projects
// Dependencies: nlohmann/json 3.x (json.hpp in this directory), C++23 (std::expected), threads.
//
// Usage:
//     struct Host : reuse::mcp::ToolHost {
//         auto list_tools() -> nlohmann::json override;                       // [{name, description, inputSchema}]
//         auto call_tool(const std::string&, const nlohmann::json&) -> std::expected<nlohmann::json, reuse::mcp::ToolError> override;
//         auto reload() -> std::expected<void, std::string> override;         // optional
//         auto raw_results() const -> bool override { return true; }           // optional, see Results
//         auto supports_list_changed() const -> bool override { return true; } // optional, see Changes
//         auto tools_changed() -> bool override;                              // optional, see Changes
//     } host;
//     reuse::mcp::StdioServer srv({"name", "1.0"}, host, std::cin, std::cout, [](std::string_view m){ log(m); });
//     signal handler: srv.request_reload();
//     return srv.run();        // returns at EOF of the input
//
// Threads: run() reads requests on the calling thread. initialize and ping are answered there at once;
//     tools/list and tools/call go through a queue to ONE worker thread, which owns the ToolHost (a Lua state
//     must stay on one thread). So a slow tool call never blocks receiving the next request. Responses are
//     written under a mutex, one line each; order of responses can differ from order of requests (allowed:
//     JSON-RPC matches by id).
// Protocol: newline-delimited JSON, no embedded newlines (dump() without indent). Batches (JSON arrays) are
//     rejected with -32600 (MCP 2025-06-18 removed batching). Supported versions: 2025-06-18, 2025-03-26,
//     2024-11-05; the server answers with the client's version if supported, else the newest.
//     Tool failures are results with isError=true (MCP spec); an unknown tool name is JSON-RPC -32602.
// Results: by default the skeleton wraps a tool's JSON value as text (compact dump) + structuredContent.
//     A host with raw_results() == true returns the complete CallToolResult itself ({content=[...],
//     isError=...}); the skeleton sends it unchanged (a value without a content array is error -32603).
// Changes: a host with supports_list_changed() == true announces capabilities.tools.listChanged = true.
//     The worker asks tools_changed() before each request and every idle_poll while idle (the host may
//     reload its registry there, e.g. on a newer file mtime); on true, and after a successful
//     request_reload(), it sends notifications/tools/list_changed once the client has sent
//     notifications/initialized. Hosts without the feature keep the former behaviour (listChanged false,
//     no notifications).
// Security: The stdio transport trusts its peer process (process boundary = trust boundary); the caller
//     decides who may start the server. Input lines over max_line_bytes (default 1 MiB) are rejected
//     without parsing. Strings with invalid UTF-8 are replaced in the output (error_handler_t::replace).
// Correctness: Tests in btop-aix tests/mcp_roundtrip.cpp (initialize, tools/list, tools/call, errors, EOF);
//     raw results, listChanged and non-blocking receive: tests of a further user of this header.
// History: git log -- cpp/mcp_stdio_server.hpp

#ifndef REUSE_MCP_STDIO_SERVER_HPP
#define REUSE_MCP_STDIO_SERVER_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <expected>
#include <functional>
#include <istream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>

#include "json.hpp"

namespace reuse::mcp {

	//? Shared type block of the MCP core. Token-identical in mcp_dispatcher.hpp and mcp_stdio_server.hpp under
	//? one guard, so a program may include both headers in any order (for example lua_mcp_tools.hpp +
	//? StdioServer). mcp_stdio_server.hpp keeps its copy only because btop-aix links single headers; once it
	//? links mcp_dispatcher.hpp as well, the copy there goes away. Change both or neither.
#ifndef REUSE_MCP_TOOL_HOST_TYPES
#define REUSE_MCP_TOOL_HOST_TYPES
	struct ServerInfo {
		std::string name;
		std::string version;
		std::string instructions;	//? optional text for the client, empty = left out
	};

	struct ToolError {
		enum class Kind { UnknownTool, InvalidArguments, Failed } kind = Kind::Failed;
		std::string message;
		nlohmann::json structured;	//? optional: Failed with a structured result (isError=true, structuredContent)
	};

	//? Who sends a request (set by an authenticating transport, e.g. a bearer token -> service account).
	//? Empty = unauthenticated local peer (stdio): ToolHost::permitted() decides what that may do.
	struct Caller {
		std::string name;
		std::string role;
	};

	class ToolHost {
	public:
		virtual ~ToolHost() = default;
		virtual auto list_tools() -> nlohmann::json = 0;
		virtual auto call_tool(const std::string& name, const nlohmann::json& arguments) -> std::expected<nlohmann::json, ToolError> = 0;
		virtual auto reload() -> std::expected<void, std::string> { return {}; }
		//* true: call_tool returns the complete CallToolResult ({content=[...], isError=...}); the skeleton sends it unchanged
		virtual auto raw_results() const -> bool { return false; }
		//* true: the skeleton asks tools_changed() before each request (StdioServer also while idle and announces listChanged)
		virtual auto supports_list_changed() const -> bool { return false; }
		//* Called on the worker thread only; true = the tool list changed since the last call (host reloaded itself)
		virtual auto tools_changed() -> bool { return false; }
		//* Called on the worker thread only: may this caller see and call the tool? Default: yes.
		//* tools/list leaves out tools that are not permitted; tools/call of one is an isError result.
		virtual auto permitted(const std::string& tool, const Caller& caller) -> bool {
			static_cast<void>(tool);
			static_cast<void>(caller);
			return true;
		}
	};

	inline constexpr std::array<std::string_view, 3> supported_versions{"2025-06-18", "2025-03-26", "2024-11-05"};
#endif // REUSE_MCP_TOOL_HOST_TYPES

	class StdioServer {
	public:
		using Logger = std::function<void(std::string_view)>;

		StdioServer(ServerInfo info, ToolHost& host, std::istream& in, std::ostream& out, Logger log = {}, size_t max_line_bytes = 1 << 20,
					std::chrono::milliseconds idle_poll = std::chrono::milliseconds(2000))
		: info(std::move(info)), host(host), in(in), out(out), log(std::move(log)), max_line_bytes(max_line_bytes), idle_poll(idle_poll),
		  list_changed(host.supports_list_changed()) {}

		StdioServer(const StdioServer&) = delete;
		StdioServer& operator=(const StdioServer&) = delete;

		//* Async-signal-safe: sets a flag; the worker reloads the host before its next request.
		void request_reload() noexcept { reload_pending.store(true, std::memory_order_relaxed); }

		//* Serve until EOF of the input; returns 0.
		auto run() -> int {
			std::thread worker([this] { work(); });
			std::string line;
			while (std::getline(in, line)) {
				if (line.size() > max_line_bytes) {
					send_error(nullptr, -32600, "request line too long");
					continue;
				}
				if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
				handle_line(line);
			}
			{
				std::lock_guard lock(queue_mtx);
				closing = true;
			}
			queue_cv.notify_all();
			worker.join();
			return 0;
		}

	private:
		void handle_line(const std::string& line) {
			nlohmann::json msg;
			try {
				msg = nlohmann::json::parse(line);
			}
			catch (const nlohmann::json::parse_error&) {
				send_error(nullptr, -32700, "Parse error");
				return;
			}
			if (not msg.is_object()) {
				send_error(nullptr, -32600, msg.is_array() ? "batches are not supported" : "request must be an object");
				return;
			}
			const bool has_id = msg.contains("id");
			const nlohmann::json id = has_id ? msg["id"] : nlohmann::json(nullptr);
			if (has_id and not (id.is_string() or id.is_number_integer())) {
				send_error(nullptr, -32600, "id must be a string or an integer");
				return;
			}
			if (msg.value("jsonrpc", "") != "2.0" or not msg.contains("method") or not msg["method"].is_string()) {
				if (has_id) send_error(id, -32600, "Invalid Request");
				return;
			}
			const std::string method = msg["method"];
			if (not has_id) {	//? notifications (notifications/initialized, cancelled, ...) need no answer
				if (method == "notifications/initialized") client_ready.store(true, std::memory_order_release);
				return;
			}

			const nlohmann::json params = msg.value("params", nlohmann::json::object());
			if (method == "initialize") {
				std::string version{supported_versions.front()};
				if (params.is_object() and params.contains("protocolVersion") and params["protocolVersion"].is_string()) {
					const std::string asked = params["protocolVersion"];
					for (const auto v : supported_versions) if (v == asked) version = asked;
				}
				nlohmann::json result = {
					{"protocolVersion", version},
					{"capabilities", {{"tools", {{"listChanged", list_changed}}}}},
					{"serverInfo", {{"name", info.name}, {"version", info.version}}},
				};
				if (not info.instructions.empty()) result["instructions"] = info.instructions;
				send_result(id, result);
			}
			else if (method == "ping") {
				send_result(id, nlohmann::json::object());
			}
			else if (method == "tools/list" or method == "tools/call") {
				{
					std::lock_guard lock(queue_mtx);
					queue.push_back({id, method, params});
				}
				queue_cv.notify_one();
			}
			else {
				send_error(id, -32601, "Method not found: " + method);
			}
		}

		struct Job {
			nlohmann::json id;
			std::string method;
			nlohmann::json params;
		};

		void work() {
			while (true) {
				Job job;
				{
					std::unique_lock lock(queue_mtx);
					const auto ready = [&] { return closing or not queue.empty(); };
					if (list_changed) {
						if (not queue_cv.wait_for(lock, idle_poll, ready)) {
							lock.unlock();
							poll_changes();
							continue;
						}
					}
					else {
						queue_cv.wait(lock, ready);
					}
					if (queue.empty()) return;	//? closing and drained
					job = std::move(queue.front());
					queue.pop_front();
				}
				if (reload_pending.exchange(false, std::memory_order_relaxed)) {
					if (auto r = host.reload(); not r) write_log("reload failed, keeping previous tools: " + r.error());
					else {
						write_log("tools reloaded");
						notify_list_changed();
					}
				}
				if (list_changed) poll_changes();
				try {
					if (job.method == "tools/list") {
						send_result(job.id, {{"tools", host.list_tools()}});
					}
					else {
						call(job);
					}
				}
				catch (const std::exception& e) {
					send_error(job.id, -32603, std::string("Internal error: ") + e.what());
				}
			}
		}

		void poll_changes() {
			try {
				if (host.tools_changed()) notify_list_changed();
			}
			catch (const std::exception& e) {
				write_log(std::string("tools_changed failed: ") + e.what());
			}
		}

		void notify_list_changed() {
			if (list_changed and client_ready.load(std::memory_order_acquire))
				send({{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"}});
		}

		void call(const Job& job) {
			if (not job.params.is_object() or not job.params.contains("name") or not job.params["name"].is_string()) {
				send_error(job.id, -32602, "params.name (string) is required");
				return;
			}
			const nlohmann::json args = job.params.value("arguments", nlohmann::json::object());
			if (not args.is_object()) {
				send_error(job.id, -32602, "params.arguments must be an object");
				return;
			}
			auto r = host.call_tool(job.params["name"].get<std::string>(), args);
			if (not r) {
				if (r.error().kind == ToolError::Kind::UnknownTool) {
					send_error(job.id, -32602, r.error().message);
					return;
				}
				send_result(job.id, {{"content", {{{"type", "text"}, {"text", r.error().message}}}}, {"isError", true}});
				return;
			}
			if (host.raw_results()) {
				if (not r->is_object() or not r->contains("content") or not (*r)["content"].is_array()) {
					send_error(job.id, -32603, "Internal error: tool result has no content array");
					return;
				}
				send_result(job.id, *r);
				return;
			}
			const nlohmann::json structured = r->is_object() ? *r : nlohmann::json{{"result", *r}};
			send_result(job.id, {
				{"content", {{{"type", "text"}, {"text", dump(*r)}}}},
				{"structuredContent", structured},
				{"isError", false},
			});
		}

		static auto dump(const nlohmann::json& j) -> std::string {
			return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		void send(const nlohmann::json& msg) {
			const std::string line = dump(msg);
			std::lock_guard lock(out_mtx);
			out << line << '\n' << std::flush;
		}
		void send_result(const nlohmann::json& id, const nlohmann::json& result) {
			send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
		}
		void send_error(const nlohmann::json& id, int code, const std::string& message) {
			send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
		}
		void write_log(std::string_view m) {
			if (log) log(m);
		}

		ServerInfo info;
		ToolHost& host;
		std::istream& in;
		std::ostream& out;
		Logger log;
		size_t max_line_bytes;
		std::chrono::milliseconds idle_poll;
		bool list_changed;
		std::atomic<bool> client_ready{false};
		std::mutex out_mtx;
		std::mutex queue_mtx;
		std::condition_variable queue_cv;
		std::deque<Job> queue;
		bool closing = false;
		std::atomic<bool> reload_pending{false};
	};

} // namespace reuse::mcp

#endif
