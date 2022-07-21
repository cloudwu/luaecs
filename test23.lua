local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "data",
	type = "raw",
	size = 8
}

w:register {
	name = "id",
	type = "int",
}

w:new {
	data = "12345678",
	id = 0,
}

for v in w:select "data id:in" do
	print(v.id)
end



