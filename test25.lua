local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "string",
	type = "lua",
}

w:register {
	name = "tag"
}

local eid = w:new {
	tag = true
}

w:import(eid, {
	string = "Hello",
	tag = false,
})

assert(w:access(eid, "string") == "Hello")
assert(w:access(eid, "tag") == false)