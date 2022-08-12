local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "A",
	type = "int",
}

w:register {
	name = "B",
	type = "int",
}

w:register {
	name = "C",
	type = "int",
}


w:new {
	A = 1,
	B = 2,
	C = 0,
}

local function add_AB(v)
	w:extend(v, "A:in B:in C:out")
	v.C = v.A + v.B
end

for v in w:select "A:in" do
	add_AB(v)
	w:submit(v)
	break
end

for v in w:select "C:in" do
	w:extend(v, "A:in B:in")
	print(v.A, v.B, v.C)
end