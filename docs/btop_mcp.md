# MCP adapter (`src/btop_mcp.*`, `src/btop_mcp_tools.hpp`, `mcp/tools.lua`)

**Since:** 2026-10-19 · **Layer:** 3 of 3 (core API -> Lua binding -> adapters) · **Tools:** `docs/mcp.md` (generated)

## Purpose

`btop --mcp-stdio` answers MCP clients with measurements of the host. An MCP client on a workstation starts
the following command as its server command:

```sh
ssh <USER>@<AIX_HOST> /opt/btop/bin/btop --mcp-stdio
```

## Parts

| Part | Where | Content |
|---|---|---|
| Skeleton | `libs/reuse/mcp_stdio_server.hpp` (single header, shared with other projects of the author) | stdio transport, JSON-RPC 2.0, initialize, ping, tools/list, tools/call, errors |
| Tool host | `reuse::mcp::LuaTools` (`libs/reuse/lua_mcp_tools.hpp`, since 2026-10-31); `src/btop_mcp_tools.hpp` sets the options of btop (no file watch, results wrapped by the skeleton) | loads and validates the registry, calls handlers in the Lua state |
| Registry | `mcp/tools.lua` (package: `/opt/btop/share/btop/mcp/tools.lua`) | curated tools: snapshot_info, cpu, memory, disks, network, processes; send_signal only with `--mcp-allow-signals` |
| Entry | `Mcp::run_stdio` in `src/btop_mcp.cpp`, called from `btop_main` after the config is loaded | headless: no `Term::init`, no locale check, no drawing |

## Flow

1. btop loads the config.
2. btop resets the TUI keys that shape the data, in memory only: `proc_tree`, `proc_filter`, `pause_proc_list`
   and `show_detailed`.
3. `Shared::init()` runs, and `Core::Loop` starts.
4. btop loads the registry. On an error btop prints a message on stderr and exits with code 1.
5. btop waits for the second snapshot, because the CPU percentages need two snapshots.
6. btop serves requests until stdin reaches EOF. Then it stops the loop.

Three threads take part. The reader (main thread) answers initialize and ping at once. One worker owns the Lua
state and runs the tools. The core thread collects. Tool calls never block the reader.

## Reload the registry at runtime

After `kill -HUP <PID>`, the worker reloads `tools.lua` before the next request. If the new file is invalid, the
previous registry stays, and btop writes a message to btop.log.

## Security

- stdio is the only transport, so the process boundary is the trust boundary. A user who may start
  `btop --mcp-stdio` gets the data that the btop user can see. Over ssh this means every user who may log in.
  btop opens no port and needs no token and no TLS.
- The writing tool `send_signal` is off by default. The OS checks the permission of the btop user.
- The registry runs in the Lua sandbox with memory and time limits (`docs/btop_lua.md`).
- A network transport would need TLS and client certificates. stdio over ssh uses the authentication and
  encryption of ssh instead. Querying an already running interactive btop would need such a network transport,
  which is not built. The adapter ships from `v1.4.8-aix1` on.

## Tests

`tests/mcp_roundtrip.cpp` runs against the real `mcp/tools.lua`. It covers initialize with version negotiation,
tools/list, tools/call, ping, unknown methods and tools, invalid arguments, batches, parse errors, send_signal on
and off, and reload.
