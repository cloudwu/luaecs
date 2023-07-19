local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:alias("vx", "vector", "x")
w:alias("vy", "vector", "y")

w:new {
	vector = { x = 1, y = 2 }
}

for e in w:select "vector:in vx:in vy:in" do
	assert(e.vector.x == e.vx)
	assert(e.vector.y == e.vy)
	print(e.vx, e.vy)
end


