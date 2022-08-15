local ecs = require "ecs"

local w = ecs.world()

w:register {
	type = "int",
	name = "A",
}

w:register {
	type = "int",
	name = "B",
}

local A = w:new { A = 1 }
local B = w:new { B = 2 }

for v in w:select "eid A?in B?in" do
	print(v.A, v.B)
end

local v = w:fetch(A, "A:in")
assert(v.A == 1)
