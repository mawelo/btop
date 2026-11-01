// __REUSABLE__
//
// mcp_dispatcher.hpp - transport-independent MCP (Model Context Protocol) core: JSON-RPC 2.0 dispatch,
//                      initialize handshake, ping, tools/list, tools/call, one worker thread for the tools.
//
// Who:     Martin Lonkwitz
// What:    Dispatcher takes one JSON-RPC message (text or parsed) plus a reply callback. initialize and
//          ping are answered at once on the calling thread; tools/list and tools/call go through a queue to
//          ONE worker thread that owns the ToolHost (a Lua state must stay on one thread). The reply
//          callback runs exactly once per request (never for notifications), possibly on the worker thread.
//          Transports: serve_stdio() below (newline-delimited JSON), mcp_http_server_qt.hpp (HTTP/HTTPS on Qt).
// When:    2026-10-19 (protocol core of mcp_stdio_server.hpp, extended for several transports)
// Why:     The MCP part of an app is split: a fixed reusable skeleton (transport, JSON-RPC,
//          handshake, errors); tools live in a script behind the ToolHost; no tool call blocks the receipt
//          of requests.
// Project: reusable; used by several projects (stdio, HTTP, HTTPS via mcp_http_server_httplib.hpp).
// Note:    mcp_stdio_server.hpp still carries its own copy of this protocol core. btop-aix linked that header
//          alone via symlink until 2026-10-31; since then it also links this file through lua_mcp_tools.hpp.
//          Since 2026-10-23 both headers share the type block (ServerInfo, ToolError, ToolHost,
//          supported_versions) token-identically under the guard REUSE_MCP_TOOL_HOST_TYPES, so lua_mcp_tools.hpp
//          (on this header) and StdioServer can be used in one program. Open: StdioServer on top of Dispatcher
//          + serve_stdio(); nothing blocks it any more.
// Results: by default a tool's JSON value is wrapped as text + structuredContent. A host with raw_results() ==
//          true returns the complete CallToolResult itself; it is sent unchanged (no content array: -32603).
// Changes: a host with supports_list_changed() == true is asked tools_changed() before each tools/list or
//          tools/call (the host reloads its registry there, e.g. on a newer file mtime). No notification is
//          sent (HTTP has no push channel without SSE); clients see the new list on their next tools/list.
// Dependencies: nlohmann/json 3.x (json.hpp in this directory), C++23 (std::expected), threads.
//
// Usage:
//     struct Host : reuse::mcp::ToolHost { ... } host;          // list_tools, call_tool, optional reload
//     reuse::mcp::Dispatcher d({"name", "1.0"}, host, [](std::string_view m){ log(m); });
//     d.start();                                                 // worker thread
//     bool answered = d.handle(text, [](const nlohmann::json& response){ send(response); });
//     ...                                                        // answered == false: notification, no reply
//     d.stop();                                                  // drains the queue, joins the worker
//     stdio: reuse::mcp::serve_stdio(d, std::cin, out);          // until EOF; starts and stops d itself
//
// Protocol: Batches (JSON arrays) are rejected with -32600 (MCP 2025-06-18 removed batching). Supported
//     versions: 2025-06-18, 2025-03-26, 2024-11-05; the server answers with the client's version if
//     supported, else the newest. Tool failures are results with isError=true (MCP spec); an unknown tool
//     name is JSON-RPC -32602. A ToolError with `structured` set keeps the structured result next to
//     isError=true (e.g. {ok:false, error:"..."} from a script handler). Error codes: -32700 parse error, -32600 invalid request (also bad id types),
//     -32601 unknown method, -32602 unknown tool / bad params, -32603 exception in the host.
// Access:  handle() takes an optional Caller {name, role} (set by an authenticating transport). The worker asks
//          ToolHost::permitted(tool, caller) before tools/call (refused: isError result, handler does not run)
//          and filters tools/list with it. The default permitted() allows everything (behaviour as before).
// Threads: handle() may be called from any thread, also concurrently. The ToolHost is only touched by the
//     worker (and by the caller before start() / after stop()). request_reload() is async-signal-safe.
// Security: No transport here; see the transport headers. Output strings with invalid UTF-8 are replaced
//     (dump() uses error_handler_t::replace) instead of throwing.
// Correctness: Tests in btop-aix tests/mcp_roundtrip.cpp (stdio) and the tests of a further user (HTTP).
// History: git log -- cpp/mcp_dispatcher.hpp cpp/mcp_stdio_server.hpp

#ifndef REUSE_MCP_DISPATCHER_HPP
#define REUSE_MCP_DISPATCHER_HPP

#include <array>
#include <cstddef>
#include <atomic>
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

	//* Compact JSON, invalid UTF-8 replaced (never throws on bad bytes)
	inline auto dump(const nlohmann::json& j) -> std::string {
		return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	inline auto error_response(const nlohmann::json& id, int code, const std::string& message) -> nlohmann::json {
		return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
	}

	inline auto result_response(const nlohmann::json& id, const nlohmann::json& result) -> nlohmann::json {
		return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
	}

	class Dispatcher {
	public:
		using Reply = std::function<void(const nlohmann::json&)>;
		using Logger = std::function<void(std::string_view)>;

		Dispatcher(ServerInfo info, ToolHost& host, Logger log = {})
		: info(std::move(info)), host(host), log(std::move(log)) {}

		Dispatcher(const Dispatcher&) = delete;
		Dispatcher& operator=(const Dispatcher&) = delete;
		~Dispatcher() { stop(); }

		//* Starts the worker thread (once).
		void start() {
			std::lock_guard lock(queue_mtx);
			if (worker.joinable() or closing) return;
			worker = std::thread([this] { work(); });
		}

		//* Lets the worker finish the queued jobs, then joins it. Idempotent. Later requests get -32603.
		void stop() {
			{
				std::lock_guard lock(queue_mtx);
				closing = true;
			}
			queue_cv.notify_all();
			if (worker.joinable() and worker.get_id() != std::this_thread::get_id()) worker.join();
		}

		//* Async-signal-safe: sets a flag; the worker reloads the host before its next request.
		void request_reload() noexcept { reload_pending.store(true, std::memory_order_relaxed); }

		//* One message as text. Returns false when no reply will come (notification, or a message
		//* without id that is invalid). Otherwise `reply` runs exactly once, maybe on the worker thread.
		//* `caller` reaches ToolHost::permitted() for tools/list and tools/call (default: empty caller).
		auto handle(std::string_view text, Reply reply, Caller caller = {}) -> bool {
			nlohmann::json msg;
			try {
				msg = nlohmann::json::parse(text);
			}
			catch (const nlohmann::json::parse_error&) {
				reply(error_response(nullptr, -32700, "Parse error"));
				return true;
			}
			return handle(msg, std::move(reply), std::move(caller));
		}

		//* One parsed message; same contract as above.
		auto handle(const nlohmann::json& msg, Reply reply, Caller caller = {}) -> bool {
			if (not msg.is_object()) {
				reply(error_response(nullptr, -32600, msg.is_array() ? "batches are not supported" : "request must be an object"));
				return true;
			}
			const bool has_id = msg.contains("id");
			const nlohmann::json id = has_id ? msg["id"] : nlohmann::json(nullptr);
			if (has_id and not (id.is_string() or id.is_number_integer())) {
				reply(error_response(nullptr, -32600, "id must be a string or an integer"));
				return true;
			}
			if (msg.value("jsonrpc", "") != "2.0" or not msg.contains("method") or not msg["method"].is_string()) {
				if (not has_id) return false;
				reply(error_response(id, -32600, "Invalid Request"));
				return true;
			}
			const std::string method = msg["method"];
			if (not has_id) return false;	//? notifications (notifications/initialized, cancelled, ...) need no answer

			const nlohmann::json params = msg.value("params", nlohmann::json::object());
			if (method == "initialize") {
				std::string version{supported_versions.front()};
				if (params.is_object() and params.contains("protocolVersion") and params["protocolVersion"].is_string()) {
					const std::string asked = params["protocolVersion"];
					for (const auto v : supported_versions) if (v == asked) version = asked;
				}
				nlohmann::json result = {
					{"protocolVersion", version},
					{"capabilities", {{"tools", {{"listChanged", false}}}}},
					{"serverInfo", {{"name", info.name}, {"version", info.version}}},
				};
				if (not info.instructions.empty()) result["instructions"] = info.instructions;
				reply(result_response(id, result));
			}
			else if (method == "ping") {
				reply(result_response(id, nlohmann::json::object()));
			}
			else if (method == "tools/list" or method == "tools/call") {
				bool queued = false;
				{
					std::lock_guard lock(queue_mtx);
					if (not closing) {
						queue.push_back({id, method, params, reply, std::move(caller)});
						queued = true;
					}
				}
				if (queued) queue_cv.notify_one();
				else reply(error_response(id, -32603, "server is shutting down"));
			}
			else {
				reply(error_response(id, -32601, "Method not found: " + method));
			}
			return true;
		}

	private:
		struct Job {
			nlohmann::json id;
			std::string method;
			nlohmann::json params;
			Reply reply;
			Caller caller;
		};

		void work() {
			while (true) {
				Job job;
				{
					std::unique_lock lock(queue_mtx);
					queue_cv.wait(lock, [&] { return closing or not queue.empty(); });
					if (queue.empty()) return;	//? closing and drained
					job = std::move(queue.front());
					queue.pop_front();
				}
				if (reload_pending.exchange(false, std::memory_order_relaxed)) {
					if (auto r = host.reload(); not r) write_log("reload failed, keeping previous tools: " + r.error());
					else write_log("tools reloaded");
				}
				if (host.supports_list_changed()) {	//? e.g. LuaTools: registry file changed -> reloaded before this job
					try {
						if (host.tools_changed()) write_log("tools reloaded (registry changed)");
					}
					catch (const std::exception& e) {
						write_log(std::string("tools_changed failed: ") + e.what());
					}
				}
				try {
					if (job.method == "tools/list") job.reply(result_response(job.id, {{"tools", listing(job.caller)}}));
					else job.reply(call(job));
				}
				catch (const std::exception& e) {
					job.reply(error_response(job.id, -32603, std::string("Internal error: ") + e.what()));
				}
			}
		}

		auto call(const Job& job) -> nlohmann::json {
			if (not job.params.is_object() or not job.params.contains("name") or not job.params["name"].is_string())
				return error_response(job.id, -32602, "params.name (string) is required");
			const nlohmann::json args = job.params.value("arguments", nlohmann::json::object());
			if (not args.is_object()) return error_response(job.id, -32602, "params.arguments must be an object");
			const std::string name = job.params["name"].get<std::string>();
			if (not host.permitted(name, job.caller)) {
				write_log("tools/call " + name + " refused for caller '" + job.caller.name + "' (role '" + job.caller.role + "')");
				return result_response(job.id, {
					{"content", {{{"type", "text"}, {"text", "tool '" + name + "' is not permitted for role '" + job.caller.role + "'"}}}},
					{"isError", true},
				});
			}
			auto r = host.call_tool(name, args);
			if (not r) {
				if (r.error().kind == ToolError::Kind::UnknownTool) return error_response(job.id, -32602, r.error().message);
				if (const auto& st = r.error().structured; not st.is_null()) {
					return result_response(job.id, {
						{"content", {{{"type", "text"}, {"text", dump(st)}}}},
						{"structuredContent", st.is_object() ? st : nlohmann::json{{"result", st}}},
						{"isError", true},
					});
				}
				return result_response(job.id, {{"content", {{{"type", "text"}, {"text", r.error().message}}}}, {"isError", true}});
			}
			if (host.raw_results()) {
				if (not r->is_object() or not r->contains("content") or not (*r)["content"].is_array())
					return error_response(job.id, -32603, "Internal error: tool result has no content array");
				return result_response(job.id, *r);
			}
			const nlohmann::json structured = r->is_object() ? *r : nlohmann::json{{"result", *r}};
			return result_response(job.id, {
				{"content", {{{"type", "text"}, {"text", dump(*r)}}}},
				{"structuredContent", structured},
				{"isError", false},
			});
		}

		//* tools/list for one caller: the host's listing without the tools it does not permit
		auto listing(const Caller& caller) -> nlohmann::json {
			nlohmann::json all = host.list_tools();
			nlohmann::json out = nlohmann::json::array();
			for (auto& t : all)
				if (t.is_object() and t.contains("name") and t["name"].is_string() and host.permitted(t["name"].get<std::string>(), caller))
					out.push_back(std::move(t));
			return out;
		}

		void write_log(std::string_view m) {
			if (log) log(m);
		}

		ServerInfo info;
		ToolHost& host;
		Logger log;
		std::thread worker;
		std::mutex queue_mtx;
		std::condition_variable queue_cv;
		std::deque<Job> queue;
		bool closing = false;
		std::atomic<bool> reload_pending{false};
	};

	//* stdio transport (MCP "stdio"): newline-delimited JSON on in/out until EOF of `in`. Starts the
	//* dispatcher, answers every request with one line (under a mutex, any thread), stops the dispatcher
	//* after EOF (queued requests are still answered). Lines over max_line_bytes: -32600 without parsing.
	//* Security: stdio trusts its peer process (process boundary = trust boundary).
	inline auto serve_stdio(Dispatcher& d, std::istream& in, std::ostream& out, size_t max_line_bytes = 1 << 20) -> int {
		std::mutex out_mtx;
		const auto send = [&](const nlohmann::json& msg) {
			const std::string line = dump(msg);
			std::lock_guard lock(out_mtx);
			out << line << '\n' << std::flush;
		};
		d.start();
		std::string line;
		while (std::getline(in, line)) {
			if (line.size() > max_line_bytes) {
				send(error_response(nullptr, -32600, "request line too long"));
				continue;
			}
			if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
			d.handle(std::string_view(line), send);
		}
		d.stop();	//? joins the worker: no reply can use `send` after this point
		return 0;
	}

} // namespace reuse::mcp

#endif
