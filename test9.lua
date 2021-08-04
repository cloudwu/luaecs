local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:register {
	name = "text",
	type = "lua",
}

w:register {
	name = "mark"
}

w:new {
	vector = { x = 1, y = 2 },
	text = "Hello World",
	mark = true,
}

for v in w:select "mark" do
	w:readall(v)
	for k,v in pairs(v) do
		print(k,v)
	end
end




