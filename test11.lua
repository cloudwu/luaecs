local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int64",
}

w:register {
	name = "data",
	type = "lua",
}

for i = 1 , 10 do
	w:new {
		id = i,
		data = "Hello " .. i,
	}
end

local function fetch(id)
	local v = w:sync("data:in", w:fetch("id", id))
	return v.data
end

for i = 1, 10 do
	print(fetch(i))
end

