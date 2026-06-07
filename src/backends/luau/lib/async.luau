local _sched = ...
local co = coroutine

local pong = function(func, callback)
    assert(type(func) == "function")
    local thread = co.create(func)
    local step
    step = function(...)
        local pack = {co.resume(thread, ...)}
        assert(pack[1], pack[2])
        if co.status(thread) == "dead" then
            if callback then callback(select(2, table.unpack(pack))) end
        else
            local thunk = pack[2]
            assert(type(thunk) == "function", "yielded value must be a thunk")
            thunk(step)
        end
    end
    step()
end

local async = function(func)
    return function(...) pong(func, ...) end
end

local await = function(thunk)
    assert(type(thunk) == "function")
    return co.yield(thunk)
end

local defer = function()
    return function(step) _sched.defer(step) end
end

local wait_for = function(seconds)
    return function(step) _sched.timer(seconds, step) end
end

local on_hook = function(sym, phase)
    return function(step) _sched.hook(sym, phase, step) end
end

return {
    async    = async,
    await    = await,
    defer    = defer,
    wait_for = wait_for,
    on_hook  = on_hook,
}