-- Petrichor.Timer
local Task = require("Petrichor.Task")

local Timer = {}

-- Handles

local next_id = 1
local function new_id()
    local id = next_id
    next_id = next_id + 1
    return id
end

local Handle = {}
Handle.__index = Handle

local cancelled = {}

function Handle:cancel()
    cancelled[self.id] = true
end

function Handle:is_cancelled()
    return cancelled[self.id] == true
end

local function new_handle()
    return setmetatable({ id = new_id() }, Handle)
end

local function release(handle)
    cancelled[handle.id] = nil
end

Timer.Handle = Handle

-- Wall-clock timers

function Timer.after(seconds, fn)
    local h = new_handle()
    Task.async(function()
        Task.sleep(seconds)
        if not h:is_cancelled() then fn(h) end
        release(h)
    end)()
    return h
end

function Timer.every(seconds, fn)
    local h = new_handle()
    Task.async(function()
        while not h:is_cancelled() do
            Task.sleep(seconds)
            if h:is_cancelled() then break end
            fn(h)
        end
        release(h)
    end)()
    return h
end

function Timer.after_then_every(seconds, interval, fn)
    local h = new_handle()
    Task.async(function()
        Task.sleep(seconds)
        if h:is_cancelled() then release(h) return end
        fn(h)
        while not h:is_cancelled() do
            Task.await(Task.wait_for(interval))
            if h:is_cancelled() then break end
            fn(h)
        end
        release(h)
    end)()
    return h
end

return Timer
