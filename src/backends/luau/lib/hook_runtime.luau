local N = require("Augment.Native")

local fieldCache = {}
local function fieldsOf(struct)
    local c = fieldCache[struct]
    if not c then
        c = {}
        for _, f in ipairs(N.fields(struct)) do c[f.name] = f end
        fieldCache[struct] = c
    end
    return c
end

local enumCache = {}
local function enumOf(qualified)
    local c = enumCache[qualified]
    if c == nil then
        local vals = N.enumValues(qualified)
        c = (next(vals) ~= nil) and vals or false
        enumCache[qualified] = c
    end
    return c
end

local View
local function readField(ptr, f)
    if f.kind == "str" then
        return N.readStr(ptr, f.offset, f.len)
    elseif f.view ~= "" then
        return View(f.view, N.read(ptr, f.offset, "ptr"))
    else
        return N.read(ptr, f.offset, f.kind)
    end
end

local function levenshtein(a, b)
    local la, lb = #a, #b
    if la == 0 then return lb end
    if lb == 0 then return la end
    local prev = {}
    for j = 0, lb do prev[j] = j end
    for i = 1, la do
        local cur = { [0] = i }
        local ca = a:sub(i, i)
        for j = 1, lb do
            local cb = b:sub(j, j)
            local cost = (ca == cb) and 0 or 1
            cur[j] = math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
        end
        prev = cur
    end
    return prev[lb]
end

local function sortedKeys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys)
    return keys
end

local function suggest(name, candidates)
    local lname = name:lower()
    local best, bestDist
    for _, cand in ipairs(candidates) do
        local d = levenshtein(lname, cand:lower())
        if bestDist == nil or d < bestDist then
            bestDist, best = d, cand
        end
    end
    if best and bestDist <= math.max(2, math.floor(#name * 0.4)) then
        return best
    end
    return nil
end

local function notFoundError(label, k, candidates, level)
    local msg = label .. " '" .. tostring(k) .. "'"
    if #candidates == 0 then
        msg = msg .. " (struct has no known fields at all... wrong view/self type?)"
    else
        local hint = suggest(tostring(k), candidates)
        if hint then
            msg = msg .. " (did you mean '" .. hint .. "'?)"
        end
        msg = msg .. "\nvalid: " .. table.concat(candidates, ", ")
    end
    error(msg, level)
end

View = function(struct, ptr)
    if ptr == nil then return nil end
    local flds = fieldsOf(struct)
    local fieldNames
    return setmetatable({}, {
        __index = function(_, k)
            if k == "__fields" then
                fieldNames = fieldNames or sortedKeys(flds)
                return fieldNames
            end
            local f = flds[k]
            if f then return readField(ptr, f) end
            local e = enumOf(struct .. "::" .. k)
            if e then return e end
            fieldNames = fieldNames or sortedKeys(flds)
            notFoundError(struct .. " has no field/enum", k, fieldNames, 2)
        end,
        __newindex = function(_, k, v)
            local f = flds[k]
            if not f then
                fieldNames = fieldNames or sortedKeys(flds)
                notFoundError(struct .. " has no writable field", k, fieldNames, 2)
            end
            if f.kind == "str" then
                N.writeStr(ptr, f.offset, f.len, v)
            else
                N.write(ptr, f.offset, f.kind, v)
            end
        end,
    })
end

local function structName(kind)
    if kind:sub(1, 7) == "struct:" then return kind:sub(8) end
    return nil
end

local function pickSelfView(declaredSelf, params)
    if declaredSelf ~= "" and next(fieldsOf(declaredSelf)) ~= nil then
        return declaredSelf
    end
    local p0 = params[1]
    if p0 and p0.view ~= "" and next(fieldsOf(p0.view)) ~= nil then
        return p0.view
    end
    return declaredSelf
end

local function specFor(mangled)
    local byName = {}
    local params = N.params(mangled)
    for i, p in ipairs(params) do
        local nm = (p.name ~= "" and p.name) or ("arg" .. (i - 1))
        byName[nm] = { index = i - 1, kind = p.kind, view = p.view }
    end

    local selfView = pickSelfView(N.selfView(mangled), params)

    local keys = { "self", "ret", "cancelled" }
    for name in pairs(byName) do keys[#keys + 1] = name end
    table.sort(keys)

    return { byName = byName, selfView = selfView, ret = N.retKind(mangled), keys = keys }
end

local function makeCtx(spec, raw)
    local state = { cancelled = raw.cancelled }
    local cache = {}
    return setmetatable({}, {
        __index = function(_, k)
            if k == "__selfType" then return spec.selfView end
            if k == "cancelled" then return state.cancelled end
            if k == "self" then
                if cache.self == nil and spec.selfView ~= "" then
                    cache.self = View(spec.selfView, raw.self)
                end
                return cache.self
            end
            if k == "ret" then
                local sn = structName(spec.ret)
                if sn then
                    if cache.ret == nil then cache.ret = View(sn, raw.ret) end
                    return cache.ret
                end
                if spec.ret == "void" then return nil end
                return N.read(raw.ret, 0, spec.ret)
            end
            local a = spec.byName[k]
            if a then
                local sn2 = structName(a.kind)
                if sn2 then
                    if cache[k] == nil then
                        cache[k] = View(sn2, N.argptr(a.index))
                    end
                    return cache[k]
                end
                if a.view ~= "" then
                    if cache[k] == nil then
                        cache[k] = View(a.view, N.arg(a.index, "ptr"))
                    end
                    return cache[k]
                end
                return N.arg(a.index, a.kind)
            end
            notFoundError("ctx has no field", k, spec.keys, 2)
        end,
        __newindex = function(_, k, v)
            if k == "cancelled" then state.cancelled = v and true or false; return end
            if k == "ret" then
                local sn = structName(spec.ret)
                if sn then return end
                if spec.ret ~= "void" then N.write(raw.ret, 0, spec.ret, v) end
                return
            end
            local a = spec.byName[k]
            if a and structName(a.kind) == nil and a.view == "" then
                N.setArg(a.index, a.kind, v)
                return
            end
            notFoundError("ctx field", k, spec.keys, 2)
        end,
    }), state
end

local function makeHandle(mangled)
    local spec = specFor(mangled)
    local h = {}
    local function add(phase, fn)
        local fired = false
        N.register(mangled, phase, function(raw)
            if h.__once and fired then return raw.cancelled end
            fired = true
            local ctx, state = makeCtx(spec, raw)
            fn(ctx)
            return state.cancelled
        end)
        return h
    end
    h.before  = function(_, fn) return add("before", fn) end
    h.after   = function(_, fn) return add("after", fn) end
    h.replace = function(_, fn) return add("replace", fn) end
    h.once    = function(_) h.__once = true; return h end
    return h
end

local function ambiguous(flat, n)
    return setmetatable({}, {
        __index = function(_, m)
            if m == "at" then
                return function(_, file)
                    local mg = N.resolveAt(flat, file)
                    if not mg then error("Hook: '" .. flat .. "' not found in '" .. file .. "'", 2) end
                    return makeHandle(mg)
                end
            elseif m == "sig" then
                return function(_, sig)
                    local mg = N.resolveSig(flat, sig)
                    if not mg then error("Hook: '" .. flat .. "' has no overload (" .. sig .. ")", 2) end
                    return makeHandle(mg)
                end
            else
                local locs = {}
                for i = 0, n - 1 do locs[#locs + 1] = N.fnLoc(flat, i) end
                error("Hook: '" .. flat .. "' is ambiguous (" .. n .. " matches: " ..
                      table.concat(locs, ", ") .. "); use :at(file) or :sig(args)", 2)
            end
        end,
    })
end

local Hook = {}

-- Read a NUL-terminated C string from a hook argument (string or NativePtr).
function Hook.readCString(value, maxLen: number?): string?
	if type(value) == "string" then
		return value
	end
	if type(value) == "userdata" or type(value) == "lightuserdata" then
		local len = maxLen or 32
		local ok, name = pcall(N.readStr, value, 0, len)
		if ok and type(name) == "string" and name ~= "" then
			return name
		end
	end
	return nil
end

setmetatable(Hook, {
	__index = function(_, flat)
		local n = N.fnCount(flat)
		if n == 0 then
			error("Hook: no such symbol '" .. flat .. "'", 2)
		end
		if n == 1 then
			return makeHandle(N.fnMangled(flat, 0))
		end
		return ambiguous(flat, n)
	end,
})

return Hook
