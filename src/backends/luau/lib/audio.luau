local _audio = ...

return {
    play       = function(path, loop) return _audio.play(path, loop or false) end,
    stop       = function(handle)     _audio.stop(handle) end,
    set_volume = function(handle, v)  _audio.set_volume(handle, v) end,
    is_playing = function(handle)     return _audio.is_playing(handle) end,
    stop_all   = function()           _audio.stop_all() end,
}
