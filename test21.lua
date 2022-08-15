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
	marshal = function (s)
		local x, y = s:match "(%d+)x(%d+)"
		return string.pack("ii", x, y)
	end,
}


w:register {
	name = "id",
	type = "int",
	-- for tempalte
	marshal = function(v)
		return tostring(v)
	end,
	unmarshal = function(s)
		local v = tonumber(s)
		return v
--		return string.pack("i", v)
	end
}

local eid = w:new {
	size = "42x24",
	id = 100,
}

local t = w:template {
	size = "3x4",
	id = 42,
}

w:template_instance(w:new(), t)

for v in w:select "size:in id:in" do
	print(v.size.x, v.size.y, v.id)
end
