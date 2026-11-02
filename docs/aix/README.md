# btop on AIX: documents

| Task or question | Document | Type |
|---|---|---|
| Build btop for AIX 7.2 and 7.3 | README of the repository, section "Compilation AIX" | How-to |
| Build btop for AIX 7.1 and 6.1 on a 7.2 host | [howto/build-for-older-aix.md](howto/build-for-older-aix.md) | How-to |
| Why a 7.1 or 6.1 binary needs a sysroot and its own libc++ | [explanation/older-aix-releases.md](explanation/older-aix-releases.md) | Explanation |
| Which AIX pitfalls the port met, and how the code answers them | [reference/porting-pitfalls.md](reference/porting-pitfalls.md) | Reference |
| Core API, Lua binding and MCP adapter | `docs/btop_core.md`, `docs/btop_lua.md`, `docs/btop_mcp.md` | Explanation |
| Functions of the Lua module `btop` | `docs/lua-api.md` | Reference |
| Tools of `btop --mcp-stdio` | `docs/mcp.md` (generated from `mcp/tools.lua`) | Reference |
