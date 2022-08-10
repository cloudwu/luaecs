local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "string",
	type = "lua",
}

local eid = w:new()

w:import(eid, {
	string = "Hello"
})

assert(w:access(eid, "string") == "Hello")