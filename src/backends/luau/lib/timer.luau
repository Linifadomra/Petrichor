local Async = require("Petrichor.Async")
local Hook  = require("Augment.Hook")

local Timer = {}

local next_id = 1
local function new_id()
    local id = next_id
    next_id = next_id + 1
    return id
end

local Handle = {}
Handle.__index = Handle

local cancelled = {} -- set of cancelled ids

function Handle:cancel()
    cancelled[self.id] = true
end

function Handle:is_cancelled()
    return cancelled[self.id] == true
end

local function new_handle()
    local h = setmetatable({ id = new_id() }, Handle)
    return h
end

local function release(handle)
    cancelled[handle.id] = nil
end

Timer.Handle = Handle

-- Wall-clock timers (Async)

function Timer.after(seconds, fn)
    local h = new_handle()
    Async.spawn(function()
        Async.sleep(seconds)
        if not h:is_cancelled() then
            fn(h)
        end
        release(h)
    end)
    return h
end

function Timer.every(seconds, fn)
    local h = new_handle()
    Async.spawn(function()
        while not h:is_cancelled() do
            Async.sleep(seconds)
            if h:is_cancelled() then break end
            fn(h)
        end
        release(h)
    end)
    return h
end

function Timer.after_then_every(seconds, interval, fn)
    local h = new_handle()
    Async.spawn(function()
        Async.sleep(seconds)
        if h:is_cancelled() then release(h) return end
        fn(h)
        while not h:is_cancelled() do
            Async.sleep(interval)
            if h:is_cancelled() then break end
            fn(h)
        end
        release(h)
    end)
    return h
end

-- Frame timers (Hook)

local frame_timers = {}

Hook.crEvents_firePreFrame:after(function()
    local i = 1
    while i <= #frame_timers do
        local t = frame_timers[i]
        if t.handle:is_cancelled() then
            release(t.handle)
            table.remove(frame_timers, i)
        else
            t.elapsed = t.elapsed + 1
            if t.elapsed >= t.interval then
                t.elapsed = 0
                t.fn(t.handle)
                if t.once or t.handle:is_cancelled() then
                    release(t.handle)
                    table.remove(frame_timers, i)
                    -- don't advance i; next element shifted into this slot
                else
                    i = i + 1
                end
            else
                i = i + 1
            end
        end
    end
end)

function Timer.after_frames(frames, fn)
    local h = new_handle()
    frame_timers[#frame_timers + 1] = {
        handle   = h,
        interval = frames,
        elapsed  = 0,
        fn       = fn,
        once     = true,
    }
    return h
end

function Timer.every_frames(frames, fn)
    local h = new_handle()
    frame_timers[#frame_timers + 1] = {
        handle   = h,
        interval = frames,
        elapsed  = 0,
        fn       = fn,
        once     = false,
    }
    return h
end

function Timer.after_frames_then_every(delay, interval, fn)
    local h = new_handle()
    -- use a one-shot for the delay, then register the repeating timer
    frame_timers[#frame_timers + 1] = {
        handle   = h,
        interval = delay,
        elapsed  = 0,
        once     = true,
        fn       = function()
            if h:is_cancelled() then return end
            fn(h)
            -- promote to repeating
            frame_timers[#frame_timers + 1] = {
                handle   = h,
                interval = interval,
                elapsed  = 0,
                fn       = fn,
                once     = false,
            }
        end,
    }
    return h
end

return Timer
