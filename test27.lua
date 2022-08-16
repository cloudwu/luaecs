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

w:register {
	name = "mark"
}

w:new {
	A = 1,
	B = 2,
	C = 0,
	mark = true,
}

w:new {
	A = 3,
	B = 4,
	C = 42,
}

local function add_AB(v)
	w:extend(v, "A:in B:in C:out")
	print("ADD", v.A, v.B)
	v.C = v.A + v.B
end

local function read_C(v)
	w:extend(v, "C:in")
	return v.C
end

for v in w:select "A:in mark?in" do
	if v.mark then
		add_AB(v)
		print("C=", read_C(v))
	end
end

for v in w:select "C:in" do
	w:extend(v, "A:in B:in")
	print(v.A, v.B, v.C)
end