-- Petrichor.Net
local _net = ...

-- TCP --

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

-- UDP --
local udp_open = function()
    return _net.udp_open()
end
local udp_send = function(sock, host, port, data)
    return _net.udp_send(sock, host, port, data)
end
local udp_recv = function(sock, max)
    return function(step)
        _net.udp_recv(sock, max or 4096, step)
    end
end
local udp_bind = function(sock, port)
    return _net.udp_bind(sock, port)
end
local udp_close = function(sock)
    _net.udp_close(sock)
end
local udp_connect = function(sock, host, port)
    return _net.udp_connect(sock, host, port)
end
local udp_send_data = function(sock, data)
    return _net.udp_send_data(sock, data)
end
local udp_recv_nonblock = function(sock, max)
    return _net.udp_recv_nonblock(sock, max or 4096)
end

return {
    connect = connect,
    recv    = recv,
    send    = send,
    close   = close,
    udp_open      = udp_open,
    udp_send      = udp_send,
    udp_connect   = udp_connect,
    udp_send_data = udp_send_data,
    udp_recv          = udp_recv,
    udp_recv_nonblock = udp_recv_nonblock,
    udp_bind          = udp_bind,
    udp_close         = udp_close
}