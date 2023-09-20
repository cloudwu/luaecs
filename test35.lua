local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "A",
	type = "int"
}

w:register {
	name = "B",
	type = "int"
}

for i = 1, 10 do
	w:new { A = i }
	w:new { B = -i }
end

for a,b in w:select2("A:in", "B:in") do
	print(a.A, b.B)
end

