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

w:register {
	name = "id",
	type = "int",
	marshal = function(v)
		return tostring(v)
	end,
	unmarshal = function(s)
		local v = tonumber(s)
		return string.pack("i", v)
	end
}

w:new {
	size = "42x24",
	id = 100,
}

local t = w:template( {
	size = "3x4",
	id = 42,
}, seri)

w:template_instance(t, deseri)

for v in w:select "size:in id:in" do
	print(v.size.x, v.size.y, v.id)
end
