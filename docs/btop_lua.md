# Lua binding (`src/btop_lua.hpp`, `src/btop_lua.cpp`)

**Since:** 2026-10-19 · **Layer:** 2 of 3 (core API -> Lua binding -> adapters) · **API reference:** `docs/lua-api.md`

## Purpose

The binding makes the core API the canonical scripting API: a `Lua::State` offers the module `btop`. An adapter
may need something that Lua cannot reach. Such a need is a gap in the core API and is closed there.

## Decisions

- btop vendors Lua 5.4.8 unchanged in `libs/lua-5.4.8`. The sha256 sums were checked against lua.org
  (`libs/lua-5.4.8/ORIGIN.txt`).
- The binding uses the plain C API of Lua and not sol2. This saves one dependency, and tables from structs need
  nothing more.
- Lua is compiled as C++ (`CMakeLists.txt`, `-x c++`). `lua_error` then throws an exception. On an error the
  stack unwinds and destroys the C++ objects in binding functions (`shared_ptr`, `std::expected`, strings).
  With `longjmp` their destructors would be skipped.
- Since 2026-10-31 the sandbox comes from a shared header (`libs/reuse/lua_sandbox.hpp`, `reuse::lua::Sandbox`).
  Before that, btop had its own copy with the same limits and the same set of libraries. The shared header was
  generalised from that copy.

## Flow

`State(Options)` builds a `reuse::lua::Sandbox` from `memory_limit`, `call_limit` and a `print` function that
writes to stderr. The sandbox counts allocations and opens only the sandbox libraries. `State` then registers
`btop`. The sandbox keeps its own pointer in the extra space of the `lua_State`. Therefore every function of
`btop` carries the `State` as upvalue 1 (light userdata, `luaL_setfuncs(L, btop_funcs, 1)`). `pcall()` arms an
instruction-count hook. The hook raises `time limit exceeded` after `call_limit` instructions.
`do_string` and `do_file` load text chunks only (mode `"t"`). `sandbox()` hands the sandbox to the MCP tool host,
so the registry runs in the same state.

## Security

- The sandbox removes `io`, `os`, `package`, `debug`, `load`, `dofile`, `loadfile` and `string.dump`. `print`
  writes to stderr.
- Lua code cannot reach the upvalue that holds the `State`, because that would need the `debug` library.
- Each state has a memory limit (default 64 MiB), and each call has a time limit (default 2 s).
- btop never loads bytecode, because malformed bytecode can crash the Lua VM.

## Tests

`tests/lua_api.cpp` covers the sandbox, every function on a fixed snapshot, the process query, signals on and off,
the memory limit, the time limit, and a round trip through the JSON bridge.
