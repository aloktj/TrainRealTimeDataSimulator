-- Example Lua hook for scripting_hooks.py
-- Expects SIM_BASE_URL env var from the runner. Emits JSON on stdout.

local base = os.getenv("SIM_BASE_URL") or "http://127.0.0.1:8000"

local function fetch(path)
    local handle = io.popen(string.format('curl -s %s%s', base, path))
    if not handle then
        return nil, "curl not available"
    end
    local data = handle:read('*a')
    handle:close()
    return data
end

local function escape(str)
    str = str or ""
    str = str:gsub('\\', '\\\\')
    str = str:gsub('"', '\\"')
    str = str:gsub('\n', '\\n')
    return str
end

local pd_resp, err = fetch('/api/pd/status')
local status = pd_resp and 'pass' or 'fail'
local details = pd_resp or err or 'no response'
local payload = string.format('[{"name":"lua pd status","status":"%s","details":"%s"}]', status, escape(details))
print(payload)
