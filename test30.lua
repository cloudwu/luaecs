local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:register {
	name = "mark"
}

w:register {
	name = "id",
	type = "int"
}

w:register {
	name = "object",
	type = "lua",
}

w:register {
	name = "name",
	type = "lua",
}

local eid = w:new {
	vector = { x = 1, y = 2 },
	mark = true,
	id = 42,
	object = { x = 3, y = 4 },
	name = "foo",
}

local a = w:accessor(eid)

assert(a.vector.x == 1)
assert(a.vector.y == 2)
assert(a.mark == true)
assert(a.id == 42)
assert(a.object.x == 3)
assert(a.object.y == 4)
assert(a.name == "foo")

a.name = "foobar"
a.vector.x = 10
a.vector.y = 20
a.id = 24
a.mark = false
a.object.x = 0

assert(a.name == "foobar")

a() -- sync

for e in w:select "vector:in mark?in id:in object:in name:in" do
	print(e.vector.x, e.vector.y)
	print(e.mark)
	print(e.id)
	print(e.object.x, e.object.y)
	print(e.name)
end
