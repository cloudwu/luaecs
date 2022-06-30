local ecs = require "ecs"

local w = ecs.world()

local function seri(v)
	assert(type(v) == "string")
	return v
end

local function deseri(v)
	return v
end


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

local t = w:template( {
	size = "3x4"
}, seri)

w:template_instance(t, deseri)

for v in w:select "size:in" do
	print(v.size.x, v.size.y)
end
