local _storage = ...
local Json = require("Petrichor.Json")

local store = {}
local dirty = false

local function load_store()
    local raw = _storage.read_file()
    if raw then
        local ok, data = pcall(Json.decode, raw)
        if ok and type(data) == "table" then
            for k, v in pairs(data) do store[k] = v end
        end
    end
end

local function save_store()
    if not dirty then return end
    local ok, encoded = pcall(Json.encode, store)
    if ok then
        _storage.write_file(encoded)
        dirty = false
    end
end

load_store()

local Storage = {}

function Storage.store(key, value)
    store[key] = value
    dirty = true
end

function Storage.retrieve(key)
    return store[key]
end

function Storage.save()
    save_store()
end

function Storage.clear(key)
    store[key] = nil
    dirty = true
end

function Storage.clear_all()
    store = {}
    dirty = true
end

function Storage.asset(path)
    return _storage.read_asset(path)
end

function Storage.asset_path(path)
    return _storage.asset_path(path)
end

function Storage.default(key, value)
    if store[key] == nil then
        store[key] = value
        dirty = true
    end
    return store[key]
end

return Storage