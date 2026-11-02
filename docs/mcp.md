# MCP tools of btop --mcp-stdio

Generated from `mcp/tools.lua` by `tools/doc/gen_mcp_md.lua` -- do not edit by hand.
Start: `ssh user@host /opt/btop/bin/btop --mcp-stdio`; transport stdio (newline-delimited JSON-RPC 2.0).
Lua API behind the tools: `docs/lua-api.md`.

## `snapshot_info`

Sequence number and time (epoch ms) of the measurement the other tools return.

No arguments.

Schema: `{"additionalProperties": false, "properties": {}, "type": "object"}`

## `cpu`

CPU model, total and per-core utilisation in percent, load average (1, 5, 15 min).

No arguments.

Schema: `{"additionalProperties": false, "properties": {}, "type": "object"}`

## `memory`

Physical memory and paging space in bytes: total, used, available, cached, free, swap_*.

No arguments.

Schema: `{"additionalProperties": false, "properties": {}, "type": "object"}`

## `disks`

Mounted file systems: mount point, device name, type, total/used/free bytes, used percent.

No arguments.

Schema: `{"additionalProperties": false, "properties": {}, "type": "object"}`

## `network`

Network interfaces: addresses, link state, current speed and total traffic in bytes.

No arguments.

Schema: `{"additionalProperties": false, "properties": {}, "type": "object"}`

## `processes`

Processes sorted and filtered like the btop process list. Default: sort 'cpu lazy', limit 20.

| Argument | Type | Required | Constraints | Description |
|---|---|---|---|---|
| `filter` | string | no |  | Substring of pid, name, command or user; '!' prefix: regular expression |
| `limit` | integer | no | >= 0, <= 1000 | Maximum number of processes (0 = all) |
| `reverse` | boolean | no |  | Reverse the sort order |
| `sort` | string | no | one of ["pid", "name", "command", "threads", "user", "memory", "cpu direct", "cpu lazy"] | Sort key (btop names) |

Schema: `{"additionalProperties": false, "properties": {"filter": {"description": "Substring of pid, name, command or user; '!' prefix: regular expression", "type": "string"}, "limit": {"description": "Maximum number of processes (0 = all)", "maximum": 1000, "minimum": 0, "type": "integer"}, "reverse": {"description": "Reverse the sort order", "type": "boolean"}, "sort": {"description": "Sort key (btop names)", "enum": ["pid", "name", "command", "threads", "user", "memory", "cpu direct", "cpu lazy"], "type": "string"}}, "type": "object"}`

## `send_signal`

*Only listed with `--mcp-allow-signals`.*

Send a signal (number) to a process; the operating system checks the permission of the btop user.

| Argument | Type | Required | Constraints | Description |
|---|---|---|---|---|
| `pid` | integer | yes | >= 1 |  |
| `signal` | integer | yes | >= 0, <= 64 | e.g. 15 = TERM, 9 = KILL |

Schema: `{"additionalProperties": false, "properties": {"pid": {"minimum": 1, "type": "integer"}, "signal": {"description": "e.g. 15 = TERM, 9 = KILL", "maximum": 64, "minimum": 0, "type": "integer"}}, "required": ["pid", "signal"], "type": "object"}`
