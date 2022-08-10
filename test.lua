package.cpath = "./?.dll"
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

w:register {
	name = "singleton",
	type = "lua"
}

local context = w:context {
	"vector",
	"mark",
	"id",
	"singleton",
	"object",
}

w:new { singleton = "Hello World" }

w:update()

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


w:new { object = "Hello" , mark = true }
w:new { object = "World" , mark = true }

w:update()

-- test c object


print "mark:update object:in"

for v in w:select "mark:update object:in" do
	print(v.object)
	if v.object == "Hello" then
		print "Disable mark where object == Hello"
		v.mark = false
	end
end

print("GETLUA",test.getlua(context, 1))
print("SIBLINGLUA",test.siblinglua(context, 1))

print "mark:exist object:in"

for v in w:select "mark:exist object:in" do
	print(v.object)
end

for v in w:select "object:exist mark:out" do
	v.mark = false
end

for v in w:select "mark:exist" do
	print("Remove")
	w:remove(v)
end

for v in w:select "REMOVED:exist vector:in" do
	print(v.vector.x, v.vector.y, "removed")
end

w:update()	-- remove all

local n = 0
for v in w:select "mark:in" do
	n = n + 1
end
print("Marked", n)


print "object:update"

for v in w:select "object:update" do
	print(v.object)
	v.object = v.object .. " world"
end

print "object:in"

for v in w:select "object:in" do
	print(v.object)
end

w:register {
	name = "sum",
	type = "float",
}

for v in w:select "vector:in sum:new" do
	print(v.vector.x, "+", v.vector.y)
	v.sum = v.vector.x + v.vector.y
end

for v in w:select "sum:in" do
	print(v.sum)
end

w:clear "sum"

for v in w:select "sum:exist" do
	error "Not empty"
end
