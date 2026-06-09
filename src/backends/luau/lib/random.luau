-- Deterministic PRNG helpers (xorshift32 and .NET System.Random compatible).

local Random = {}

local Xorshift = {}
Xorshift.__index = Xorshift

function Xorshift.new(seed: number?)
	local self = setmetatable({}, Xorshift)
	self._state = seed or 0
	if self._state == 0 then
		self._state = 0x6C8E9CF5
	end
	return self
end

function Xorshift:next(): number
	local x = self._state
	x = bit32.bxor(x, bit32.lshift(x, 13))
	x = bit32.bxor(x, bit32.rshift(x, 17))
	x = bit32.bxor(x, bit32.lshift(x, 5))
	self._state = x
	return x
end

function Xorshift:nextFloat(): number
	return (self:next() % 0x1000000) / 0x1000000
end

local System = {}
System.__index = System

function System.new(seed: number)
	local self = setmetatable({}, System)
	self._seed = seed % 2147483647
	if self._seed <= 0 then
		self._seed = 1
	end
	return self
end

function System:_sample(): number
	self._seed = (self._seed * 214013 + 2531011) % 2147483647
	return bit32.rshift(self._seed, 16)
end

function System:next(maxExclusive: number): number
	if maxExclusive <= 0 then
		return 0
	end
	return math.floor((self:_sample() / 32768) * maxExclusive)
end

Random.xorshift = Xorshift
Random.system = System

-- Legacy module-level xorshift (seed once, call Random.next()).
local globalState = 0

function Random.seed(value: number)
	globalState = value
	if globalState == 0 then
		globalState = 0x6C8E9CF5
	end
end

function Random.next(): number
	local x = globalState
	x = bit32.bxor(x, bit32.lshift(x, 13))
	x = bit32.bxor(x, bit32.rshift(x, 17))
	x = bit32.bxor(x, bit32.lshift(x, 5))
	globalState = x
	return x
end

function Random.nextFloat(): number
	return (Random.next() % 0x1000000) / 0x1000000
end

return Random
