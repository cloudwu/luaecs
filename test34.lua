local ecs_core = require "ecs.core"
ecs_core.DEBUG = true	-- turn on DEBUG

local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:new {
	vector = {
		x = 1,
		y = 2
	}
}

assert(pcall(w.new, w, { vector = { x=1,y=2,z=3 } }) == false)
