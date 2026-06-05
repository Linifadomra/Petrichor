local Hook = require("Augment.Hook")

Hook.Mob_tick:before(function(ctx)
    local m = ctx.self
    if m.hp ~= m.State.dead then
        m.hp = 0
        m.name = "slain"
    end
    ctx.cancelled = true
end)
