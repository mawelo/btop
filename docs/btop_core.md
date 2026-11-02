# Core API (`src/btop_core.hpp`, `src/btop_core.cpp`)

**Since:** 2026-10-19 · **Layer:** 1 of 3 (core API -> Lua binding -> adapters)

## Purpose

The core API gives UI-free access to everything that btop measures. It also offers the one action that btop
performs, which is sending signals. Three users sit on top of this layer: the TUI menu, the Lua API, and the MCP
adapter, which reaches the core through the Lua API. No feature exists only in the UI.

## Flow

- `collect_snapshot(seq)` calls the existing collectors (`Cpu/Mem/Net/Proc::collect`) once. It copies the parts
  that an API needs into a `Snapshot` (plain structs, no deques, no UI state).
- `Core::Loop` owns the collectors on its own thread. It collects every `interval_ms` and runs posted commands in
  between, in order. `snapshot()` hands out a `shared_ptr<const Snapshot>` and copies only the pointer under a
  mutex. Readers never touch collector state.
- `call(fn)` posts `fn` to the core thread and waits for its result (`std::packaged_task`). Exceptions reach the
  caller.
- `query_processes()` filters a copy of the process list (`Proc::matches_filter`), sorts it (`Proc::proc_sorter`)
  and limits it. The cost is O(n log n).
- `send_signal(pid, sig)` checks that pid >= 1 and 0 <= sig <= 64, then calls `kill()`. On failure it returns
  errno. Both signal dialogs in `btop_menu.cpp` and `btop.send_signal` in Lua use this function.

## Correctness

- The collectors keep static state and are not thread-safe. Therefore only the core thread calls them. In TUI mode
  the runner thread calls them as before, and the loop does not start.
- `post()` after `stop()` throws. Otherwise it would queue a command that nobody runs. Commands that were queued
  before `stop()` still run.
- Tests: `tests/core_loop.cpp` checks order, thread, exceptions, concurrent callers and stop. `tests/lua_api.cpp`
  checks the limits of `send_signal`. On 2026-10-31, with the C++26 build on Linux and clang 22, `ctest` passed
  19 of 19 tests in an ASan/UBSan build and 19 of 19 in a TSan build, one run each.

## Security

`send_signal` grants nothing beyond the OS permission of the btop user. Under MCP the function is off unless
btop starts with `--mcp-allow-signals`.
