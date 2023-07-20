local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:new {
	vector = { x = 1, y = 2 }
}

-- vector_x is alias for vector.x
-- vector_y is alias for vector.y
for e in w:select "vector:in vector_x:in vector_y:out" do
	assert(e.vector.x == e.vector_x)
	print(e.vector_x)
	e.vector_y = - e.vector_x
end

for e in w:select "vector:in vector_x:in vector_y:in" do
	assert(e.vector.x == e.vector_x)
	assert(e.vector.y == e.vector_y)
	assert(e.vector_x == - e.vector_y)
	print(e.vector_x, e.vector_y)
end




