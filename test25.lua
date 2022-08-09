local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "string",
	type = "lua",
}

local id = w:new()

w:add(id, {
	string = "Hello"
})

assert(t == id)
assert(w:access(id, "string") == "Hello")