-- tools.lua -- Projekt btop-aix (2026-10-19)
--
-- Wer/Was : MCP tool registry of btop --mcp-stdio. Returns a list of tools; each has name, description,
--           inputSchema (JSON schema as Lua table) and handler(args) -> result table. Handlers use only the
--           Lua API of btop (module "btop", reference docs/lua-api.md).
-- Warum   : Code rule "App-Architektur: Scripting-API & MCP": tools are curated and live in a script; a new
--           tool is a change here, no rebuild. Reload at runtime: kill -HUP <pid of btop --mcp-stdio>.
-- Sandbox : no io, os, package, debug, load, dofile, loadfile, string.dump; time limit 2 s per call,
--           memory limit 64 MiB for the state (src/btop_lua.cpp).
-- Doku    : docs/mcp.md is generated from this file: lua5.4 tools/doc/gen_mcp_md.lua mcp/tools.lua > docs/mcp.md
-- Install : /opt/btop/share/btop/mcp/tools.lua (package), or btop --mcp-tools FILE

local object, array = btop.object, btop.array

-- Schema for a tool without arguments
local function no_args()
	return object{ type = "object", properties = object{}, additionalProperties = false }
end

-- Reject arguments the schema does not list; clients may skip schema validation
local function check_keys(args, allowed)
	for k in pairs(args) do
		if not allowed[k] then error("unknown argument: " .. tostring(k), 0) end
	end
end

local tools = {}

tools[#tools + 1] = {
	name = "snapshot_info",
	description = "Sequence number and time (epoch ms) of the measurement the other tools return.",
	inputSchema = no_args(),
	handler = function(args)
		check_keys(args, {})
		local s = btop.snapshot_info()
		s.version = btop.version()
		return s
	end,
}

tools[#tools + 1] = {
	name = "cpu",
	description = "CPU model, total and per-core utilisation in percent, load average (1, 5, 15 min).",
	inputSchema = no_args(),
	handler = function(args)
		check_keys(args, {})
		return btop.cpu()
	end,
}

tools[#tools + 1] = {
	name = "memory",
	description = "Physical memory and paging space in bytes: total, used, available, cached, free, swap_*.",
	inputSchema = no_args(),
	handler = function(args)
		check_keys(args, {})
		return btop.memory()
	end,
}

tools[#tools + 1] = {
	name = "disks",
	description = "Mounted file systems: mount point, device name, type, total/used/free bytes, used percent.",
	inputSchema = no_args(),
	handler = function(args)
		check_keys(args, {})
		return object{ disks = btop.disks() }
	end,
}

tools[#tools + 1] = {
	name = "network",
	description = "Network interfaces: addresses, link state, current speed and total traffic in bytes.",
	inputSchema = no_args(),
	handler = function(args)
		check_keys(args, {})
		return object{ interfaces = btop.network() }
	end,
}

tools[#tools + 1] = {
	name = "processes",
	description = "Processes sorted and filtered like the btop process list. Default: sort 'cpu lazy', limit 20.",
	inputSchema = object{
		type = "object",
		properties = object{
			sort = object{ type = "string", enum = btop.sort_keys(), description = "Sort key (btop names)" },
			reverse = object{ type = "boolean", description = "Reverse the sort order" },
			filter = object{ type = "string", description = "Substring of pid, name, command or user; '!' prefix: regular expression" },
			limit = object{ type = "integer", minimum = 0, maximum = 1000, description = "Maximum number of processes (0 = all)" },
		},
		additionalProperties = false,
	},
	handler = function(args)
		check_keys(args, { sort = true, reverse = true, filter = true, limit = true })
		local limit = args.limit or 20
		if math.type(limit) ~= "integer" or limit < 0 or limit > 1000 then error("limit must be an integer 0..1000", 0) end
		return object{
			processes = btop.processes{ sort = args.sort, reverse = args.reverse, filter = args.filter, limit = limit },
		}
	end,
}

-- Writing tool: only listed when btop runs with --mcp-allow-signals
if btop.signals_allowed then
	tools[#tools + 1] = {
		name = "send_signal",
		description = "Send a signal (number) to a process; the operating system checks the permission of the btop user.",
		inputSchema = object{
			type = "object",
			properties = object{
				pid = object{ type = "integer", minimum = 1 },
				signal = object{ type = "integer", minimum = 0, maximum = 64, description = "e.g. 15 = TERM, 9 = KILL" },
			},
			required = array{ "pid", "signal" },
			additionalProperties = false,
		},
		handler = function(args)
			check_keys(args, { pid = true, signal = true })
			if math.type(args.pid) ~= "integer" or math.type(args.signal) ~= "integer" then
				error("pid and signal must be integers", 0)
			end
			local ok, err, errno = btop.send_signal(args.pid, args.signal)
			if not ok then error(string.format("send_signal: %s (errno %d)", err, errno), 0) end
			return object{ sent = true, pid = args.pid, signal = args.signal }
		end,
	}
end

return tools
