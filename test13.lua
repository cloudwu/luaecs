local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int64",
}

w:register {
	name = "value",
	type = "int",
}

local index = w:make_index "id"

w:new {
	id = 1,
	value = 42,
}

w:new {
	id = 2,
	value = 100,
}

local v = w:sync("value:in", index[1])
assert(v.value == 42)
w:remove(v)
w:update()
assert(index[1] == nil)
local v = w:sync("value:in", index[2])
assert(v.value == 100)


