-- Petrichor.Math
local Math = {}

-- Scalar

Math.pi      = math.pi
Math.tau     = math.pi * 2
Math.epsilon = 1e-6

function Math.clamp(v, lo, hi)   return math.max(lo, math.min(hi, v)) end
function Math.lerp(a, b, t)      return a + (b - a) * t end
function Math.remap(v, a, b, c, d) return c + (v - a) / (b - a) * (d - c) end
function Math.rad(deg)           return deg * (math.pi / 180) end
function Math.deg(rad)           return rad * (180 / math.pi) end
function Math.sign(v)            return v > 0 and 1 or v < 0 and -1 or 0 end
function Math.approx(a, b)       return math.abs(a - b) < Math.epsilon end

function Math.wrap_angle(a)
    a = a % Math.tau
    if a > math.pi then a = a - Math.tau end
    return a
end

-- Vec2

local Vec2 = {}
Vec2.__index = Vec2

function Math.vec2(x, y)
    return setmetatable({ x = x or 0, y = y or 0 }, Vec2)
end

function Vec2:__tostring() return ("Vec2(%g, %g)"):format(self.x, self.y) end
function Vec2:__add(b)     return Math.vec2(self.x + b.x, self.y + b.y) end
function Vec2:__sub(b)     return Math.vec2(self.x - b.x, self.y - b.y) end
function Vec2:__unm()      return Math.vec2(-self.x, -self.y) end
function Vec2:__eq(b)      return Math.approx(self.x, b.x) and Math.approx(self.y, b.y) end

function Vec2:__mul(s)
    if type(s) == "number" then return Math.vec2(self.x * s, self.y * s) end
    return Math.vec2(self.x * s.x, self.y * s.y)
end

function Vec2:length_sq() return self.x*self.x + self.y*self.y end
function Vec2:length()    return math.sqrt(self:length_sq()) end
function Vec2:dot(b)      return self.x*b.x + self.y*b.y end

function Vec2:normalize()
    local l = self:length()
    if l < Math.epsilon then return Math.vec2(0, 0) end
    return Math.vec2(self.x/l, self.y/l)
end

function Vec2:lerp(b, t)  return Math.vec2(Math.lerp(self.x, b.x, t), Math.lerp(self.y, b.y, t)) end
function Vec2:distance(b) return (self - b):length() end
function Vec2:angle()     return math.atan2(self.y, self.x) end
function Vec2:clone()     return Math.vec2(self.x, self.y) end

function Vec2:saturate_length()
    return Math.clamp(self:length(), 0, 1)
end

Math.Vec2 = Vec2

-- Vec3

local Vec3 = {}
Vec3.__index = Vec3

function Math.vec3(x, y, z)
    return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, Vec3)
end

Math.vec3_zero  = function() return Math.vec3(0, 0, 0) end
Math.vec3_up    = function() return Math.vec3(0, 1, 0) end
Math.vec3_right = function() return Math.vec3(1, 0, 0) end
Math.vec3_fwd   = function() return Math.vec3(0, 0, -1) end

function Vec3:__tostring() return ("Vec3(%g, %g, %g)"):format(self.x, self.y, self.z) end
function Vec3:__add(b)     return Math.vec3(self.x+b.x, self.y+b.y, self.z+b.z) end
function Vec3:__sub(b)     return Math.vec3(self.x-b.x, self.y-b.y, self.z-b.z) end
function Vec3:__unm()      return Math.vec3(-self.x, -self.y, -self.z) end

function Vec3:__eq(b)
    return Math.approx(self.x, b.x) and Math.approx(self.y, b.y) and Math.approx(self.z, b.z)
end

function Vec3:__mul(s)
    if type(s) == "number" then return Math.vec3(self.x*s, self.y*s, self.z*s) end
    return Math.vec3(self.x*s.x, self.y*s.y, self.z*s.z)
end

function Vec3:length_sq() return self.x*self.x + self.y*self.y + self.z*self.z end
function Vec3:length()    return math.sqrt(self:length_sq()) end
function Vec3:dot(b)      return self.x*b.x + self.y*b.y + self.z*b.z end

function Vec3:cross(b)
    return Math.vec3(
        self.y*b.z - self.z*b.y,
        self.z*b.x - self.x*b.z,
        self.x*b.y - self.y*b.x
    )
end

function Vec3:normalize()
    local l = self:length()
    if l < Math.epsilon then return Math.vec3(0, 0, 0) end
    return Math.vec3(self.x/l, self.y/l, self.z/l)
end

function Vec3:lerp(b, t)
    return Math.vec3(Math.lerp(self.x,b.x,t), Math.lerp(self.y,b.y,t), Math.lerp(self.z,b.z,t))
end

function Vec3:distance(b)   return (self - b):length() end
function Vec3:distance_sq(b) return (self - b):length_sq() end
function Vec3:clone()       return Math.vec3(self.x, self.y, self.z) end

function Vec3:project_onto(b)
    local d = b:dot(b)
    if d < Math.epsilon then return Math.vec3() end
    return b * (self:dot(b) / d)
end

function Vec3:reflect(normal)
    return self - normal * (2 * self:dot(normal))
end

Math.Vec3 = Vec3

-- Quat

local Quat = {}
Quat.__index = Quat

function Math.quat(x, y, z, w)
    return setmetatable({ x = x or 0, y = y or 0, z = z or 0, w = w or 1 }, Quat)
end

function Math.quat_identity() return Math.quat(0, 0, 0, 1) end

function Math.quat_from_axis_angle(axis, angle)
    local a = axis:normalize()
    local s = math.sin(angle * 0.5)
    return Math.quat(a.x*s, a.y*s, a.z*s, math.cos(angle * 0.5))
end

function Math.quat_from_euler_zyx(z, y, x)
    -- ZYX intrinsic (GC convention)
    local cx, sx = math.cos(x*0.5), math.sin(x*0.5)
    local cy, sy = math.cos(y*0.5), math.sin(y*0.5)
    local cz, sz = math.cos(z*0.5), math.sin(z*0.5)
    return Math.quat(
        sx*cy*cz - cx*sy*sz,
        cx*sy*cz + sx*cy*sz,
        cx*cy*sz - sx*sy*cz,
        cx*cy*cz + sx*sy*sz
    )
end

function Math.quat_look_at(from, to, up)
    up = up or Math.vec3_up()
    local fwd = (to - from):normalize()
    local right = up:cross(fwd):normalize()
    local u = fwd:cross(right)
    local m00,m01,m02 = right.x, right.y, right.z
    local m10,m11,m12 = u.x,     u.y,     u.z
    local m20,m21,m22 = fwd.x,   fwd.y,   fwd.z
    local trace = m00 + m11 + m22
    local qx,qy,qz,qw
    if trace > 0 then
        local s = 0.5 / math.sqrt(trace + 1)
        qw = 0.25 / s
        qx = (m12 - m21) * s
        qy = (m20 - m02) * s
        qz = (m01 - m10) * s
    elseif m00 > m11 and m00 > m22 then
        local s = 2 * math.sqrt(1 + m00 - m11 - m22)
        qw = (m12 - m21) / s
        qx = 0.25 * s
        qy = (m01 + m10) / s
        qz = (m20 + m02) / s
    elseif m11 > m22 then
        local s = 2 * math.sqrt(1 + m11 - m00 - m22)
        qw = (m20 - m02) / s
        qx = (m01 + m10) / s
        qy = 0.25 * s
        qz = (m12 + m21) / s
    else
        local s = 2 * math.sqrt(1 + m22 - m00 - m11)
        qw = (m01 - m10) / s
        qx = (m20 + m02) / s
        qy = (m12 + m21) / s
        qz = 0.25 * s
    end
    return Math.quat(qx, qy, qz, qw):normalize()
end

function Quat:__tostring() return ("Quat(%g, %g, %g, %g)"):format(self.x, self.y, self.z, self.w) end

function Quat:__mul(b)
    if getmetatable(b) == Quat then
        return Math.quat(
            self.w*b.x + self.x*b.w + self.y*b.z - self.z*b.y,
            self.w*b.y - self.x*b.z + self.y*b.w + self.z*b.x,
            self.w*b.z + self.x*b.y - self.y*b.x + self.z*b.w,
            self.w*b.w - self.x*b.x - self.y*b.y - self.z*b.z
        )
    elseif getmetatable(b) == Vec3 then
        return self:rotate_vec3(b)
    end
end

function Quat:length_sq() return self.x*self.x + self.y*self.y + self.z*self.z + self.w*self.w end
function Quat:length()    return math.sqrt(self:length_sq()) end

function Quat:normalize()
    local l = self:length()
    if l < Math.epsilon then return Math.quat_identity() end
    return Math.quat(self.x/l, self.y/l, self.z/l, self.w/l)
end

function Quat:conjugate() return Math.quat(-self.x, -self.y, -self.z, self.w) end
function Quat:inverse()   return self:conjugate():normalize() end
function Quat:dot(b)      return self.x*b.x + self.y*b.y + self.z*b.z + self.w*b.w end
function Quat:clone()     return Math.quat(self.x, self.y, self.z, self.w) end

function Quat:rotate_vec3(v)
    local qv = Math.vec3(self.x, self.y, self.z)
    local uv = qv:cross(v)
    local uuv = qv:cross(uv)
    return v + (uv * (2 * self.w)) + (uuv * 2)
end

function Quat:to_euler_zyx()
    local x = self.x; local y = self.y; local z = self.z; local w = self.w
    local sinr_cosp = 2*(w*x + y*z)
    local cosr_cosp = 1 - 2*(x*x + y*y)
    local rx = math.atan2(sinr_cosp, cosr_cosp)
    local sinp = 2*(w*y - z*x)
    local ry = math.abs(sinp) >= 1 and math.pi/2 * Math.sign(sinp) or math.asin(sinp)
    local siny_cosp = 2*(w*z + x*y)
    local cosy_cosp = 1 - 2*(y*y + z*z)
    local rz = math.atan2(siny_cosp, cosy_cosp)
    return rx, ry, rz
end

function Quat:slerp(b, t)
    local dot = Math.clamp(self:dot(b), -1, 1)
    local b2 = b
    if dot < 0 then
        dot = -dot
        b2 = Math.quat(-b.x, -b.y, -b.z, -b.w)
    end
    if dot > 1 - Math.epsilon then
        -- linear fallback
        return Math.quat(
            Math.lerp(self.x, b2.x, t),
            Math.lerp(self.y, b2.y, t),
            Math.lerp(self.z, b2.z, t),
            Math.lerp(self.w, b2.w, t)
        ):normalize()
    end
    local theta = math.acos(dot)
    local st    = math.sin(theta)
    local wa    = math.sin((1-t)*theta) / st
    local wb    = math.sin(t*theta)    / st
    return Math.quat(
        self.x*wa + b2.x*wb,
        self.y*wa + b2.y*wb,
        self.z*wa + b2.z*wb,
        self.w*wa + b2.w*wb
    )
end

function Quat:to_mat4() return Math.mat4_from_quat(self) end

Math.Quat = Quat

-- Mat4
-- Column-major, stored as m[1..16]
-- m[col*4 + row + 1]  (0-indexed col/row)

local Mat4 = {}
Mat4.__index = Mat4

local function mat4_raw(t)
    return setmetatable(t, Mat4)
end

function Math.mat4_identity()
    return mat4_raw({
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1,
    })
end

function Math.mat4_zero()
    return mat4_raw({0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0})
end

-- column-major index helper
local function idx(col, row) return col*4 + row + 1 end

function Mat4:get(col, row) return self[idx(col, row)] end
function Mat4:set(col, row, v) self[idx(col, row)] = v end

function Mat4:__tostring()
    local rows = {}
    for r = 0, 3 do
        rows[r+1] = ("| %8.4f %8.4f %8.4f %8.4f |"):format(
            self[idx(0,r)], self[idx(1,r)], self[idx(2,r)], self[idx(3,r)])
    end
    return table.concat(rows, "\n")
end

function Mat4:__mul(b)
    if getmetatable(b) == Mat4 then
        local r = Math.mat4_zero()
        for col = 0, 3 do
            for row = 0, 3 do
                local v = 0
                for k = 0, 3 do v = v + self[idx(k,row)] * b[idx(col,k)] end
                r[idx(col,row)] = v
            end
        end
        return r
    elseif getmetatable(b) == Vec3 then
        return self:transform_point(b)
    end
end

function Mat4:clone()
    local t = {}
    for i = 1, 16 do t[i] = self[i] end
    return mat4_raw(t)
end

function Math.mat4_translation(tx, ty, tz)
    local m = Math.mat4_identity()
    m[idx(3,0)] = tx; m[idx(3,1)] = ty; m[idx(3,2)] = tz
    return m
end

function Math.mat4_scale(sx, sy, sz)
    local m = Math.mat4_identity()
    m[idx(0,0)] = sx; m[idx(1,1)] = sy; m[idx(2,2)] = sz
    return m
end

function Math.mat4_rotation_x(a)
    local m = Math.mat4_identity()
    local c, s = math.cos(a), math.sin(a)
    m[idx(1,1)] = c;  m[idx(2,1)] = -s
    m[idx(1,2)] = s;  m[idx(2,2)] =  c
    return m
end

function Math.mat4_rotation_y(a)
    local m = Math.mat4_identity()
    local c, s = math.cos(a), math.sin(a)
    m[idx(0,0)] = c;  m[idx(2,0)] =  s
    m[idx(0,2)] = -s; m[idx(2,2)] =  c
    return m
end

function Math.mat4_rotation_z(a)
    local m = Math.mat4_identity()
    local c, s = math.cos(a), math.sin(a)
    m[idx(0,0)] = c;  m[idx(1,0)] = -s
    m[idx(0,1)] = s;  m[idx(1,1)] =  c
    return m
end

function Math.mat4_from_quat(q)
    local q = q:normalize()
    local x,y,z,w = q.x,q.y,q.z,q.w
    local x2,y2,z2 = x+x, y+y, z+z
    local xx,yy,zz = x*x2, y*y2, z*z2
    local xy,xz,yz = x*y2, x*z2, y*z2
    local wx,wy,wz = w*x2, w*y2, w*z2
    return mat4_raw({
        1-(yy+zz), xy+wz,    xz-wy,    0,
        xy-wz,     1-(xx+zz),yz+wx,    0,
        xz+wy,     yz-wx,    1-(xx+yy),0,
        0,         0,        0,        1,
    })
end

function Math.mat4_trs(translation, rotation, scale)
    local mt = Math.mat4_translation(translation.x, translation.y, translation.z)
    local mr = Math.mat4_from_quat(rotation)
    local ms = Math.mat4_scale(scale.x, scale.y, scale.z)
    return mt * mr * ms
end

function Mat4:transform_point(v)
    local x = self[idx(0,0)]*v.x + self[idx(1,0)]*v.y + self[idx(2,0)]*v.z + self[idx(3,0)]
    local y = self[idx(0,1)]*v.x + self[idx(1,1)]*v.y + self[idx(2,1)]*v.z + self[idx(3,1)]
    local z = self[idx(0,2)]*v.x + self[idx(1,2)]*v.y + self[idx(2,2)]*v.z + self[idx(3,2)]
    return Math.vec3(x, y, z)
end

function Mat4:transform_dir(v)
    -- no translation
    local x = self[idx(0,0)]*v.x + self[idx(1,0)]*v.y + self[idx(2,0)]*v.z
    local y = self[idx(0,1)]*v.x + self[idx(1,1)]*v.y + self[idx(2,1)]*v.z
    local z = self[idx(0,2)]*v.x + self[idx(1,2)]*v.y + self[idx(2,2)]*v.z
    return Math.vec3(x, y, z)
end

function Mat4:translation()
    return Math.vec3(self[idx(3,0)], self[idx(3,1)], self[idx(3,2)])
end

function Mat4:transpose()
    local m = Math.mat4_zero()
    for c = 0, 3 do for r = 0, 3 do m[idx(r,c)] = self[idx(c,r)] end end
    return m
end

function Mat4:inverse()
    -- cofactor expansion
    local m = self
    local function e(c,r) return m[idx(c,r)] end
    local t = {}
    t[ 1] =  e(1,1)*(e(2,2)*e(3,3)-e(3,2)*e(2,3)) - e(2,1)*(e(1,2)*e(3,3)-e(3,2)*e(1,3)) + e(3,1)*(e(1,2)*e(2,3)-e(2,2)*e(1,3))
    t[ 2] = -e(0,1)*(e(2,2)*e(3,3)-e(3,2)*e(2,3)) + e(2,1)*(e(0,2)*e(3,3)-e(3,2)*e(0,3)) - e(3,1)*(e(0,2)*e(2,3)-e(2,2)*e(0,3))
    t[ 3] =  e(0,1)*(e(1,2)*e(3,3)-e(3,2)*e(1,3)) - e(1,1)*(e(0,2)*e(3,3)-e(3,2)*e(0,3)) + e(3,1)*(e(0,2)*e(1,3)-e(1,2)*e(0,3))
    t[ 4] = -e(0,1)*(e(1,2)*e(2,3)-e(2,2)*e(1,3)) + e(1,1)*(e(0,2)*e(2,3)-e(2,2)*e(0,3)) - e(2,1)*(e(0,2)*e(1,3)-e(1,2)*e(0,3))
    local det = e(0,0)*t[1] + e(1,0)*t[2] + e(2,0)*t[3] + e(3,0)*t[4]
    if math.abs(det) < Math.epsilon then return Math.mat4_identity() end
    local inv = 1 / det
    local r = Math.mat4_zero()
    r[1]  = t[1]*inv
    r[2]  = t[2]*inv
    r[3]  = t[3]*inv
    r[4]  = t[4]*inv
    r[5]  = (-e(1,0)*(e(2,2)*e(3,3)-e(3,2)*e(2,3)) + e(2,0)*(e(1,2)*e(3,3)-e(3,2)*e(1,3)) - e(3,0)*(e(1,2)*e(2,3)-e(2,2)*e(1,3)))*inv
    r[6]  = ( e(0,0)*(e(2,2)*e(3,3)-e(3,2)*e(2,3)) - e(2,0)*(e(0,2)*e(3,3)-e(3,2)*e(0,3)) + e(3,0)*(e(0,2)*e(2,3)-e(2,2)*e(0,3)))*inv
    r[7]  = (-e(0,0)*(e(1,2)*e(3,3)-e(3,2)*e(1,3)) + e(1,0)*(e(0,2)*e(3,3)-e(3,2)*e(0,3)) - e(3,0)*(e(0,2)*e(1,3)-e(1,2)*e(0,3)))*inv
    r[8]  = ( e(0,0)*(e(1,2)*e(2,3)-e(2,2)*e(1,3)) - e(1,0)*(e(0,2)*e(2,3)-e(2,2)*e(0,3)) + e(2,0)*(e(0,2)*e(1,3)-e(1,2)*e(0,3)))*inv
    r[9]  = ( e(1,0)*(e(2,1)*e(3,3)-e(3,1)*e(2,3)) - e(2,0)*(e(1,1)*e(3,3)-e(3,1)*e(1,3)) + e(3,0)*(e(1,1)*e(2,3)-e(2,1)*e(1,3)))*inv
    r[10] = (-e(0,0)*(e(2,1)*e(3,3)-e(3,1)*e(2,3)) + e(2,0)*(e(0,1)*e(3,3)-e(3,1)*e(0,3)) - e(3,0)*(e(0,1)*e(2,3)-e(2,1)*e(0,3)))*inv
    r[11] = ( e(0,0)*(e(1,1)*e(3,3)-e(3,1)*e(1,3)) - e(1,0)*(e(0,1)*e(3,3)-e(3,1)*e(0,3)) + e(3,0)*(e(0,1)*e(1,3)-e(1,1)*e(0,3)))*inv
    r[12] = (-e(0,0)*(e(1,1)*e(2,3)-e(2,1)*e(1,3)) + e(1,0)*(e(0,1)*e(2,3)-e(2,1)*e(0,3)) - e(2,0)*(e(0,1)*e(1,3)-e(1,1)*e(0,3)))*inv
    r[13] = (-e(1,0)*(e(2,1)*e(3,2)-e(3,1)*e(2,2)) + e(2,0)*(e(1,1)*e(3,2)-e(3,1)*e(1,2)) - e(3,0)*(e(1,1)*e(2,2)-e(2,1)*e(1,2)))*inv
    r[14] = ( e(0,0)*(e(2,1)*e(3,2)-e(3,1)*e(2,2)) - e(2,0)*(e(0,1)*e(3,2)-e(3,1)*e(0,2)) + e(3,0)*(e(0,1)*e(2,2)-e(2,1)*e(0,2)))*inv
    r[15] = (-e(0,0)*(e(1,1)*e(3,2)-e(3,1)*e(1,2)) + e(1,0)*(e(0,1)*e(3,2)-e(3,1)*e(0,2)) - e(3,0)*(e(0,1)*e(1,2)-e(1,1)*e(0,2)))*inv
    r[16] = ( e(0,0)*(e(1,1)*e(2,2)-e(2,1)*e(1,2)) - e(1,0)*(e(0,1)*e(2,2)-e(2,1)*e(0,2)) + e(2,0)*(e(0,1)*e(1,2)-e(1,1)*e(0,2)))*inv
    return r
end

Math.Mat4 = Mat4

-- AABB

local AABB = {}
AABB.__index = AABB

function Math.aabb(min, max)
    return setmetatable({ min = min:clone(), max = max:clone() }, AABB)
end

function AABB:center()   return (self.min + self.max) * 0.5 end
function AABB:extents()  return (self.max - self.min) * 0.5 end
function AABB:size()     return self.max - self.min end

function AABB:contains(p)
    return p.x >= self.min.x and p.x <= self.max.x
       and p.y >= self.min.y and p.y <= self.max.y
       and p.z >= self.min.z and p.z <= self.max.z
end

function AABB:intersects(b)
    return self.min.x <= b.max.x and self.max.x >= b.min.x
       and self.min.y <= b.max.y and self.max.y >= b.min.y
       and self.min.z <= b.max.z and self.max.z >= b.min.z
end

function AABB:expand(p)
    return Math.aabb(
        Math.vec3(math.min(self.min.x,p.x), math.min(self.min.y,p.y), math.min(self.min.z,p.z)),
        Math.vec3(math.max(self.max.x,p.x), math.max(self.max.y,p.y), math.max(self.max.z,p.z))
    )
end

Math.AABB = AABB

return Math
