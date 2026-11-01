# Lua API of btop (module `btop`)

**Status:** single source of truth of the API surface; adapters such as MCP reach btop only through it. Every change to
`src/btop_lua.cpp` changes this file in the same commit. **Since:** btop-aix 2026-10-19.

Lua 5.4.8 (`libs/lua-5.4.8`, compiled as C++), plain C API, no sol2. The state is a sandbox:

| Available | Removed |
|---|---|
| base (without `load`, `dofile`, `loadfile`; `print` writes to stderr, because stdout carries JSON-RPC under `--mcp-stdio`), `string` (without `string.dump`), `table`, `math`, `utf8`, `coroutine` | `io`, `os`, `package`/`require`, `debug` |

Limits: 64 MiB memory for the whole state (allocator), 2 s per protected call (instruction hook every
10 000 instructions). Exceeding a limit raises a Lua error; the state stays usable.

All values come from one immutable snapshot of the core (`Core::Snapshot`, `src/btop_core.hpp`), refreshed every
`update_ms`. Functions raise `no measurement yet` before the first collection. Sizes are bytes, percentages 0..100
(per core; `total_percent` over all cores).

## Measurements (read only)

| Function | Returns |
|---|---|
| `btop.version()` | string, e.g. `"1.4.7+ee103cb"` (same as `btop --version`) |
| `btop.snapshot_info()` | `{seq, time_ms}`: number of the collection (1, 2, ...) and its epoch time in ms |
| `btop.cpu()` | `{name, total_percent, core_count, core_percent = array, load_avg = array of 3}` |
| `btop.memory()` | `{total, used, available, cached, free, swap_total, swap_used, swap_free}` |
| `btop.disks()` | array of `{mount, name, fstype, total, used, free, used_percent}` in the order of the TUI |
| `btop.network()` | array of `{iface, ipv4, ipv6, connected, download_speed, upload_speed, download_total, upload_total}`; speed in bytes/s, totals since start of btop |
| `btop.processes(query)` | array of `{pid, ppid, name, cmd, user, cpu_percent, mem_bytes, threads, state}` |
| `btop.sort_keys()` | array of valid `sort` values (`Proc::sort_vector`) |

`query` (optional table, all fields optional):

| Field | Type | Default | Meaning |
|---|---|---|---|
| `sort` | string | `"cpu lazy"` | one of `btop.sort_keys()`; unknown key raises an error |
| `reverse` | boolean | `false` | reverse order |
| `filter` | string | `""` | as the TUI filter: substring of pid, name, command or user; `!` prefix = regular expression |
| `limit` | integer >= 0 | `0` (all) | maximum number of entries |

Process names and commands are already sanitised by the collector (control characters and invalid UTF-8 -> `?`).

## Command

| Function | Returns |
|---|---|
| `btop.send_signal(pid, sig)` | `true`, or `nil, message, errno` when `kill()` fails. Raises an error unless btop runs with `--mcp-allow-signals` (`btop.signals_allowed`), and for pid < 1 or sig outside 0..64 |

The signal runs on the core thread (`Core::Loop::call`), the same function the TUI menu uses
(`Core::send_signal`). The operating system checks the permission of the user running btop.

## Fields and helpers

| Name | Meaning |
|---|---|
| `btop.signals_allowed` | boolean, set at start |
| `btop.object(t)`, `btop.array(t)` | mark table `t` (or a new one) as JSON object / array, so an empty table keeps its kind in MCP results (`libs/reuse/lua_json_bridge.hpp`) |
| `btop.null` | JSON-null sentinel for table values (`{x = btop.null}` keeps the key; `nil` would drop it); from lib b75f104 |

## Example

```lua
local top = btop.processes{ sort = "memory", limit = 3 }
for _, p in ipairs(top) do print(p.pid, p.name, p.mem_bytes) end   -- print goes to stderr
```

Tests: `tests/lua_api.cpp` (ctest), one case per function plus sandbox, limits and JSON bridge.
