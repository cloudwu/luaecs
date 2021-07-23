local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "object",
	type = "int",
	ref = true,
}

for i = 1, 10 do
	w:ref("object",  { object = i * 10 })
end

w:order("order", "object", { 9,7,5,3,1 })

for v in w:select "order object:in" do
	print(v.object)
end