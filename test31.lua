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

local t = w:template {
	vector = { x = 100, y = 200 }
}

w:template_instance(w:new(), t, { vector_x = 42 })

print("Inc Vector.x")

for e in w:select "vector:in" do
	w:extend(e, "vector_x:out")
	print(e.vector.x, e.vector.y)
	e.vector_x = e.vector.x + 1
end

print("Read Vector")

for e in w:select "vector:in" do
	print(e.vector.x, e.vector.y)
end


