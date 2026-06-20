-- Petrichor.Standard (_G; no require necessary)
local _log = require("Petrichor.Log")

_G.Levels = {
    DEBUG = "debug",
    INFO = "info",
    WARN = "warn",
    ERR  = "err",
}

_G.log = function(level, name, msg)
    local n = name or "mod"
    if string.lower(level) == Levels.ERR then
        _log.err(n, msg)
    elseif string.lower(level) == Levels.WARN then
        _log.warn(n, msg)
    elseif string.lower(level) == Levels.DEBUG then
        _log.debug(n,msg)
    else
        _log.info(n, msg)
    end
end

_G.newclass = function(name, base)
    local cls = {}
    cls.__name = name
    cls.__index = cls

    if base then
        setmetatable(cls, { __index = base })
        cls.__base = base
    end

    cls.new = function(...)
        local self = setmetatable({}, cls)

        self.super = function()
            return setmetatable({}, {
                __index = function(_, k)
                    local fn = base and base[k]
                    if type(fn) == "function" then
                        return function(...)
                            return fn(self, ...)
                        end
                    end
                    return fn
                end
            })
        end

        self.log = function(level, msg)
            log(level,cls.__name,msg)
        end

        if cls.init then
            cls.init(self, ...)
        end

        return self
    end

    setmetatable(cls, {
        __index = base,
        __call = function(_, ...)
            return cls.new(...)
        end,
    })

    return cls
end

_G.mod = function(name)
    return newclass(name,require("Petrichor.Mod"))
end