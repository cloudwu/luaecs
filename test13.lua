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

w:register {
	name = "object",
	type = "lua",
}

w:register {
	name = "point",
	"x:float",
	"y:float",
}

w:register {
	name = "tag"
}

local index = w:make_index "id"

w:new {
	id = 1,
	value = 42,
	object = "Hello",
}

w:new {
	id = 2,
	value = 100,
	point = { x = 1, y = 2 },
	tag = true,
}

-- read value
assert(index(2, "value") == 100)
-- write value
index(2, "value", 101)
assert(index(1, "object") == "Hello")
index(1, "object", "Hello World")
assert(index(1, "point") == nil)

index(1 , "tag", true)

local v = w:sync("value:in", index[2])
assert(v.value == 101)
local v = w:sync("value:in object:in tag:in", index[1])
assert(v.value == 42)
assert(v.object == "Hello World")
assert(v.tag == true)

w:remove(v)
w:update()
assert(index[1] == nil)
local v = w:sync("value:in point:in", index[2])
assert(v.value == 101)
assert(index[2].point.y == 2)

index(2, "tag", false)
