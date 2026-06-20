-- Petrichor.Mod
local Mod = {}
Mod.__name = "Mod"
Mod.__index = Mod

-- override
function Mod:init(delta) end
function Mod:tick()      end
function Mod:shutdown()  end

-- Hook registration helpers that bind to this mod instance
-- and use the runtime's mixin.register path under the hood
function Mod:before(sym, fn, priority)
    local Hook = require("Augment.Hook")
    Hook[sym]:before(function(ctx)
        fn(ctx)
    end, priority or 0)
end

function Mod:after(sym, fn, priority)
    local Hook = require("Augment.Hook")
    Hook[sym]:after(function(ctx)
        fn(ctx)
    end, priority or 0)
end

function Mod:replace(sym, fn, priority)
    local Hook = require("Augment.Hook")
    Hook[sym]:replace(function(ctx)
        fn(ctx)
    end, priority or 0)
end

-- storage helper: internally calls Petrichor.Storage save
function Mod:save()
    local ok, Storage = pcall(require, "Petrichor.Storage")
    if ok and Storage then Storage.save() end
end

-- read/write typed fields on a ctx.self pointer
-- without needing to know offsets manually
function Mod:field(ptr, offset, kind)
    local Mixin = require("Augment.Mixin")
    return Mixin.read(ptr, offset, kind)
end

function Mod:setField(ptr, offset, kind, value)
    local Mixin = require("Augment.Mixin")
    Mixin.write(ptr, offset, kind, value)
end

-- Mod Events --

local Event = require("Petrichor.Events")

function Mod:fire(event, data)
    Event.fire(self.__name .. ":" .. event, data)
end

function Mod:on(event, mod_name, fn)
    Event.on(mod_name .. ":" .. event, fn)
end

function Mod:once(event, mod_name, fn)
    Event.once(mod_name .. ":" .. event, fn)
end

function Mod:off(event, mod_name, fn)
    Event.off(mod_name .. ":" .. event, fn)
end

return Mod