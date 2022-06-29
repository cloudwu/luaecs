local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "size",
	"x:int",
	"y:int",
	init = function (s)
		local x, y = s:match "(%d+)x(%d+)"
		return { x = tonumber(x), y = tonumber(y) }
	end,
}

w:new {
	size = "42x24"
}

for v in w:select "size:in" do
	print(v.size.x, v.size.y)
end
