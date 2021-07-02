local ecs = require "ecs"

local N = 100000

local w = ecs.world()
print("memory:", w:memory())

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:register {
	name = "mark"
}

w:register {
	name = "id",
	type = "int"
}

local t = {}
for i = 1, N do
	w:new {
		vector = {
			x = 1,
			y = 2,
		}
	}
	t[i] = { x = 1, y = 2 }
end

w:update()

local function swap_c()
	for v in w:each "vector" do
		v.x, v.y = v.y, v.x
	end
end

local function swap_lua()
	for _, v in ipairs(t) do
		v.x, v.y = v.y, v.x
	end
end

local function timing(f)
	local c = os.clock()
	for i = 1, 100 do
		f()
	end
	return os.clock() - c
end

print("memory:", w:memory())

print("CSWAP", timing(swap_c))
print("LUASWAP", timing(swap_lua))

w:new {
	vector = {
		x = 3,
		y = 4,
	},
	id = 100,
}

w:new {
	vector = {
		x = 5,
		y = 6,
	},
	mark = true,
}

w:update()

local context = w:context {
	"vector",
	"mark",
	"id",
}
local test = require "ecs.ctest"
local function csum()
	return test.sum(context)
end
print("csum = ", csum())

local function luasum()
	local s = 0
	for v in w:each "vector" do
		s = s + v.x + v.y
	end
	return s
end
print("luasum = ", luasum())

print("CSUM", timing(csum))
print("LUASUM", timing(luasum))