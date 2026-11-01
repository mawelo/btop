-- gen_mcp_md.lua -- Projekt btop-aix (2026-10-19)
--
-- Wer/Was : Generates docs/mcp.md from the MCP tool registry mcp/tools.lua (code rule: docs/mcp.md is generated
--           from tools.lua, not maintained by hand). Loads the registry with a stub of the btop module
--           (signals_allowed = true, so the optional send_signal tool is documented too).
-- Aufruf  : lua5.4 tools/doc/gen_mcp_md.lua mcp/tools.lua > docs/mcp.md
-- Abh.    : Lua 5.4 interpreter on the documentation host (Debian package lua5.4); no btop binary needed.

local path = assert(arg[1], "usage: lua5.4 gen_mcp_md.lua mcp/tools.lua")

local OBJ, ARR = {}, {}
local function mark(mt) return function(t) return setmetatable(t or {}, mt) end end

-- Sort keys straight from Proc::sort_vector (SSOT), so the enum in the docs cannot drift from the code
local function sort_keys_from_source()
	local src = (path:match("^(.*)/mcp/[^/]+$") or ".") .. "/src/btop_shared.cpp"
	local f = assert(io.open(src, "r"), "cannot read " .. src)
	local text = f:read("a")
	f:close()
	local body = assert(text:match("Proc::sort_vector%s*=%s*(%b{})"), "Proc::sort_vector not found in " .. src)
	local keys = {}
	for k in body:gmatch('"([^"]+)"') do keys[#keys + 1] = k end
	return keys
end
local sort_keys = sort_keys_from_source()

local stub = {
	object = mark(OBJ),
	array = mark(ARR),
	signals_allowed = true,
	sort_keys = function() return setmetatable({ table.unpack(sort_keys) }, ARR) end,
}
local env = setmetatable({ btop = stub }, { __index = _G })
local chunk = assert(loadfile(path, "t", env))
local tools = chunk()

local function render(v)
	if type(v) == "table" then
		if getmetatable(v) == ARR or (#v > 0 and getmetatable(v) ~= OBJ) then
			local parts = {}
			for _, x in ipairs(v) do parts[#parts + 1] = render(x) end
			return "[" .. table.concat(parts, ", ") .. "]"
		end
		local keys = {}
		for k in pairs(v) do keys[#keys + 1] = k end
		table.sort(keys)
		local parts = {}
		for _, k in ipairs(keys) do parts[#parts + 1] = string.format("%q: %s", k, render(v[k])) end
		return "{" .. table.concat(parts, ", ") .. "}"
	elseif type(v) == "string" then
		return string.format("%q", v)
	end
	return tostring(v)
end

local out = {}
local function p(s) out[#out + 1] = s or "" end
p("# MCP tools of btop --mcp-stdio")
p()
p("Generated from `" .. path .. "` by `tools/doc/gen_mcp_md.lua` -- do not edit by hand.")
p("Start: `ssh user@host /opt/btop/bin/btop --mcp-stdio`; transport stdio (newline-delimited JSON-RPC 2.0).")
p("Lua API behind the tools: `docs/lua-api.md`.")
p()
for _, t in ipairs(tools) do
	p("## `" .. t.name .. "`")
	p()
	if t.name == "send_signal" then p("*Only listed with `--mcp-allow-signals`.*"); p() end
	p(t.description)
	p()
	local props = t.inputSchema.properties or {}
	local names = {}
	for k in pairs(props) do names[#names + 1] = k end
	table.sort(names)
	if #names == 0 then
		p("No arguments.")
	else
		local required = {}
		for _, r in ipairs(t.inputSchema.required or {}) do required[r] = true end
		p("| Argument | Type | Required | Constraints | Description |")
		p("|---|---|---|---|---|")
		for _, n in ipairs(names) do
			local s = props[n]
			local c = {}
			if s.enum then c[#c + 1] = "one of " .. render(s.enum) end
			if s.minimum then c[#c + 1] = ">= " .. s.minimum end
			if s.maximum then c[#c + 1] = "<= " .. s.maximum end
			p(string.format("| `%s` | %s | %s | %s | %s |", n, s.type or "", required[n] and "yes" or "no",
				table.concat(c, ", "), s.description or ""))
		end
	end
	p()
	p("Schema: `" .. render(t.inputSchema) .. "`")
	p()
end
io.write(table.concat(out, "\n"))
