-- Petrichor.Net
local _net = ...
local Async = require("Petrichor.Async")

-- connect(host, port) -> socket
local connect = function(host, port)
    return function(step)
        _net.connect(host, port, step)
    end
end

-- recv(sock, max_bytes?) -> data, err
local recv = function(sock, max)
    return function(step)
        _net.recv(sock, max or 4096, step)
    end
end

-- send and close are synchronous, no await needed
local send  = function(sock, data) _net.send(sock, data) end
local close = function(sock)       _net.close(sock)      end

return {
    connect = connect,
    recv    = recv,
    send    = send,
    close   = close,
}