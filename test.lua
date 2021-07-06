local ecs = require "ecs"

local N = 1

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

w:register {
	name = "object",
	type = "lua",
}

w:new { object = "Hello" }

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
	for v in w:select "vector:update" do
		local vec = v.vector
		vec.x, vec.y = vec.y, vec.x
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

table.insert(t, { x = 3, y = 4 })

w:new {
	vector = {
		x = 5,
		y = 6,
	},
	mark = true,
}

table.insert(t, { x = 5, y = 6 })

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
	for v in w:select "vector:in" do
		s = s + v.vector.x + v.vector.y
	end
	return s
end

print("luasum = ", luasum())

local function luanativesum()
	local s = 0
	for _, v in ipairs(t) do
		s = s + v.x + v.y
	end
	return s
end

print("lnative sum = ", luanativesum())

print("CSUM", timing(csum))
print("LUASUM", timing(luasum))
print("LNATIVESUM", timing(luanativesum))

print "vector:update"
for v in w:select "vector:update" do
	local vec = v.vector
	print(vec.x, vec.y)
	vec.x, vec.y = vec.y , vec.x
end

print "vector:in id?out"
for v in w:select "vector:in id?out" do
	print(v.vector.x, v.vector.y, v.id)
	if v.id then
		v.id = 200
	end
end

print "vector:in id:in"

for v in w:select "vector:in id:in" do
	print(v.vector.x, v.vector.y, v.id)
end

print "object:update"

for v in w:select "object:update" do
	print(v.object)
	v.object = v.object .. " world"
end

print "object:in"

for v in w:select "object:in" do
	print(v.object)
end
