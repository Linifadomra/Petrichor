-- MD5 and SHA-256 helpers (Petrichor.Hash).

local Hash = {}

local SEED_HASH_SALT = "seedHashSecret"

local function bytesToInt(bytes: { number }, offset: number): number
	local result = 0
	for i = 0, 3 do
		result += bytes[offset + i + 1] * (2 ^ (8 * (3 - i)))
	end
	return result
end

-- Compact MD5 (Luau port of standard algorithm).
local function md5Bytes(message: string): { number }
	local s = {
		7,
		12,
		17,
		22,
		7,
		12,
		17,
		22,
		7,
		12,
		17,
		22,
		7,
		12,
		17,
		22,
		5,
		9,
		14,
		20,
		5,
		9,
		14,
		20,
		5,
		9,
		14,
		20,
		5,
		9,
		14,
		20,
		4,
		11,
		16,
		23,
		4,
		11,
		16,
		23,
		4,
		11,
		16,
		23,
		4,
		11,
		16,
		23,
		6,
		10,
		15,
		21,
		6,
		10,
		15,
		21,
		6,
		10,
		15,
		21,
		6,
		10,
		15,
		21,
	}
	local K = {}
	for i = 0, 63 do
		K[i + 1] = math.floor(math.abs(math.sin(i + 1)) * 2 ^ 32) % 2 ^ 32
	end

	local function leftRotate(x, c)
		return bit32.bor(bit32.lshift(x, c), bit32.rshift(x, 32 - c)) % 2 ^ 32
	end

	local msgLen = #message
	local padded = message .. string.char(0x80)
	while (#padded % 64) ~= 56 do
		padded ..= string.char(0)
	end
	local bitLen = msgLen * 8
	for i = 0, 7 do
		padded ..= string.char(bit32.band(bit32.rshift(bitLen, i * 8), 0xFF))
	end

	local a0, b0, c0, d0 = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
	for chunk = 1, #padded, 64 do
		local M = {}
		for i = 0, 15 do
			local o = chunk + i * 4
			M[i + 1] = string.byte(padded, o)
				+ string.byte(padded, o + 1) * 256
				+ string.byte(padded, o + 2) * 65536
				+ string.byte(padded, o + 3) * 16777216
		end
		local A, B, C, D = a0, b0, c0, d0
		for i = 0, 63 do
			local F, g
			if i < 16 then
				F = bit32.bor(bit32.band(B, C), bit32.band(bit32.bnot(B), D))
				g = i
			elseif i < 32 then
				F = bit32.bor(bit32.band(D, B), bit32.band(bit32.bnot(D), C))
				g = (5 * i + 1) % 16
			elseif i < 48 then
				F = bit32.bxor(B, bit32.bxor(C, D))
				g = (3 * i + 5) % 16
			else
				F = bit32.bxor(C, bit32.bor(B, bit32.bnot(D)))
				g = (7 * i) % 16
			end
			F = (F + A + K[i + 1] + M[g + 1]) % 2 ^ 32
			A = D
			D = C
			C = B
			B = (B + leftRotate(F, s[i + 1])) % 2 ^ 32
		end
		a0 = (a0 + A) % 2 ^ 32
		b0 = (b0 + B) % 2 ^ 32
		c0 = (c0 + C) % 2 ^ 32
		d0 = (d0 + D) % 2 ^ 32
	end

	local out = {}
	for _, v in ipairs({ a0, b0, c0, d0 }) do
		for i = 0, 3 do
			table.insert(out, bit32.band(bit32.rshift(v, i * 8), 0xFF))
		end
	end
	return out
end

local function sha256Bytes(message: string): { number }
	local function rrotate(x, n)
		return bit32.bor(bit32.rshift(x, n), bit32.lshift(x, 32 - n)) % 2 ^ 32
	end
	local K = {
		0x428a2f98,
		0x71374491,
		0xb5c0fbcf,
		0xe9b5dba5,
		0x3956c25b,
		0x59f111f1,
		0x923f82a4,
		0xab1c5ed5,
		0xd807aa98,
		0x12835b01,
		0x243185be,
		0x550c7dc3,
		0x72be5d74,
		0x80deb1fe,
		0x9bdc06a7,
		0xc19bf174,
		0xe49b69c1,
		0xefbe4786,
		0x0fc19dc6,
		0x240ca1cc,
		0x2de92c6f,
		0x4a7484aa,
		0x5cb0a9dc,
		0x76f988da,
		0x983e5152,
		0xa831c66d,
		0xb00327c8,
		0xbf597fc7,
		0xc6e00bf3,
		0xd5a79147,
		0x06ca6351,
		0x14292967,
		0x27b70a85,
		0x2e1b2138,
		0x4d2c6dfc,
		0x53380d13,
		0x650a7354,
		0x766a0abb,
		0x81c2c92e,
		0x92722c85,
		0xa2bfe8a1,
		0xa81a664b,
		0xc24b8b70,
		0xc76c51a3,
		0xd192e819,
		0xd6990624,
		0xf40e3585,
		0x106aa070,
		0x19a4c116,
		0x1e376c08,
		0x2748774c,
		0x34b0bcb5,
		0x391c0cb3,
		0x4ed8aa4a,
		0x5b9cca4f,
		0x682e6ff3,
		0x748f82ee,
		0x78a5636f,
		0x84c87814,
		0x8cc70208,
		0x90befffa,
		0xa4506ceb,
		0xbef9a3f7,
		0xc67178f2,
	}
	local h = {
		0x6a09e667,
		0xbb67ae85,
		0x3c6ef372,
		0xa54ff53a,
		0x510e527f,
		0x9b05688c,
		0x1f83d9ab,
		0x5be0cd19,
	}
	local msgLen = #message
	local padded = message .. string.char(0x80)
	while (#padded % 64) ~= 56 do
		padded ..= string.char(0)
	end
	local bitLen = msgLen * 8
	for i = 7, 0, -1 do
		padded ..= string.char(bit32.band(bit32.rshift(bitLen, i * 8), 0xFF))
	end
	for chunk = 1, #padded, 64 do
		local w = {}
		for i = 0, 15 do
			local o = chunk + i * 4
			w[i + 1] = string.byte(padded, o) * 16777216
				+ string.byte(padded, o + 1) * 65536
				+ string.byte(padded, o + 2) * 256
				+ string.byte(padded, o + 3)
		end
		for i = 17, 64 do
			local s0 = bit32.bxor(rrotate(w[i - 15], 7), rrotate(w[i - 15], 18), bit32.rshift(w[i - 15], 3))
			local s1 = bit32.bxor(rrotate(w[i - 2], 17), rrotate(w[i - 2], 19), bit32.rshift(w[i - 2], 10))
			w[i] = (w[i - 16] + s0 + w[i - 7] + s1) % 2 ^ 32
		end
		local a, b, c, d, e, f, g, hh = h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8]
		for i = 1, 64 do
			local S1 = bit32.bxor(rrotate(e, 6), rrotate(e, 11), rrotate(e, 25))
			local ch = bit32.bxor(bit32.band(e, f), bit32.band(bit32.bnot(e), g))
			local t1 = (hh + S1 + ch + K[i] + w[i]) % 2 ^ 32
			local S0 = bit32.bxor(rrotate(a, 2), rrotate(a, 13), rrotate(a, 22))
			local maj = bit32.bxor(bit32.band(a, b), bit32.band(a, c), bit32.band(b, c))
			local t2 = (S0 + maj) % 2 ^ 32
			hh = g
			g = f
			f = e
			e = (d + t1) % 2 ^ 32
			d = c
			c = b
			b = a
			a = (t1 + t2) % 2 ^ 32
		end
		h[1] = (h[1] + a) % 2 ^ 32
		h[2] = (h[2] + b) % 2 ^ 32
		h[3] = (h[3] + c) % 2 ^ 32
		h[4] = (h[4] + d) % 2 ^ 32
		h[5] = (h[5] + e) % 2 ^ 32
		h[6] = (h[6] + f) % 2 ^ 32
		h[7] = (h[7] + g) % 2 ^ 32
		h[8] = (h[8] + hh) % 2 ^ 32
	end
	local out = {}
	for _, v in ipairs(h) do
		for i = 3, 0, -1 do
			table.insert(out, bit32.band(bit32.rshift(v, i * 8), 0xFF))
		end
	end
	return out
end

function Hash.hashSeed(seedStr: string, isRaceSeed: boolean): number
	local bytes
	if isRaceSeed then
		bytes = sha256Bytes(seedStr .. SEED_HASH_SALT)
	else
		bytes = md5Bytes(seedStr)
	end
	return bytesToInt(bytes, 0)
end

return Hash
