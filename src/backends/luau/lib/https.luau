-- Petrichor.Https
local native = ...

local function request(method, url, opts)
	return function(step)
		native.request(method, url, opts or {}, step)
	end
end

local M = {}

-- every function returns a thunk, so use it with Async.await.
-- Result: await(...) -> res | nil, err
-- res = { status, ok, body, headers } (header names are lowercased)
function M.request(method, url, opts) return request(method, url, opts) end
function M.get(url, opts)    return request("GET", url, opts) end
function M.head(url, opts)   return request("HEAD", url, opts) end
function M.post(url, opts)   return request("POST", url, opts) end
function M.put(url, opts)    return request("PUT", url, opts) end
function M.patch(url, opts)  return request("PATCH", url, opts) end
function M.delete(url, opts) return request("DELETE", url, opts) end

return M
