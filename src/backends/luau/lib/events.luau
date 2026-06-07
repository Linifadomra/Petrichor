local listeners = {}

local function on(event, fn)
    if not listeners[event] then listeners[event] = {} end
    table.insert(listeners[event], { fn = fn, once = false })
end

local function once(event, fn)
    if not listeners[event] then listeners[event] = {} end
    table.insert(listeners[event], { fn = fn, once = true })
end

local function off(event, fn)
    if not listeners[event] then return end
    local t = listeners[event]
    for i = #t, 1, -1 do
        if t[i].fn == fn then table.remove(t, i) end
    end
end

local function fire(event, data)
    if not listeners[event] then return end
    local t = listeners[event]
    local i = 1
    while i <= #t do
        t[i].fn(data)
        if t[i] and t[i].once then
            table.remove(t, i)
        else
            i = i + 1
        end
    end
end

return {
    on   = on,
    once = once,
    off  = off,
    fire = fire,
}