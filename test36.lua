local ecs = require "ecs"

local w = ecs.world {
	A = 2,
	B = 1,
}

w:register {
	name = "A",
	type = "int"
}

w:register {
	name = "B",
	type = "int"
}

w:register {
	name = "C",
	type = "int"
}

assert(w:component_id "A" == 2)
assert(w:component_id "B" == 1)
assert(w:component_id "C" == 3)
