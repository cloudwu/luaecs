local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}


local r = {}

for i=1, 42 do
	r[i] = {}
	w:new {
		value = i,
		reference = r[i],
	}
	w:new {
		value = -i,
	}
end

for v in w:select("value:in") do
	print(v.value)
	if v.value < 0 then
		w:remove(v)
	end
end

local function read(i)
	local v = w:sync("value:in", r[i])
	return v.value
end

w:update()

w:remove(r[1])
w:remove(r[10])
w:remove(r[20])
w:remove(r[30])

w:remove_reference(r[40])

w:update()

for i=1,42 do
	print(pcall(read,i))
end

for v in w:select("value:in") do
	print(v.value)
end
