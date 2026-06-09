-- 6-bit ASCII bit packing (used by TPR s-settings and other compact encodings).

local Bits = {}

Bits.CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_"

local function padBits(bits: string, length: number): string
	while #bits < length do
		bits = "0" .. bits
	end
	return string.sub(bits, -length)
end

function Bits.toBits(number: number, strLength: number): string
	local n = math.floor(tonumber(number) or 0)
	local out = ""
	if n == 0 then
		return padBits("0", strLength)
	end
	while n > 0 do
		out = tostring(n % 2) .. out
		n = math.floor(n / 2)
	end
	return padBits(out, strLength)
end

function Bits.encode(bitString: string): string
	local missing = (6 - (#bitString % 6)) % 6
	bitString = bitString .. string.rep("0", missing)
	local charString = ""
	local index = 1
	while index <= #bitString do
		local chunk = string.sub(bitString, index, index + 5)
		local charIndex = tonumber(chunk, 2) + 1
		charString = charString .. string.sub(Bits.CHARSET, charIndex, charIndex)
		index += 6
	end
	return charString
end

function Bits.decode(encoded: string): string
	local bits = ""
	for i = 1, #encoded do
		local idx = string.find(Bits.CHARSET, string.sub(encoded, i, i), 1, true)
		if not idx then
			error("invalid 6-bit character")
		end
		local n = idx - 1
		local chunk = ""
		for _ = 1, 6 do
			chunk = tostring(n % 2) .. chunk
			n = math.floor(n / 2)
		end
		bits ..= chunk
	end
	return bits
end

function Bits.decodeInt(encoded: string): number
	return tonumber(Bits.decode(encoded), 2) or 0
end

function Bits.newProcessor(bits: string)
	local index = 1
	local done = false
	local function need(len: number)
		if done or index + len - 1 > #bits then
			error("not enough bits")
		end
	end
	return {
		nextInt = function(_, numBits: number): number
			need(numBits)
			local slice = string.sub(bits, index, index + numBits - 1)
			index += numBits
			if index > #bits then
				done = true
			end
			return tonumber(slice, 2) or 0
		end,
		nextBool = function(_): boolean
			need(1)
			local value = string.sub(bits, index, index) == "1"
			index += 1
			if index > #bits then
				done = true
			end
			return value
		end,
		remaining = function()
			return string.sub(bits, index)
		end,
	}
end

return Bits
