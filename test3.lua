local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "2x"
}

w:register {
	name = "3x"
}

w:register {
	name = "tag"
}

for i = 1, 20 do
	w:new {
		value = i,
		["2x"] = (i % 2 == 0),
		["3x"] = (i % 3 == 0),
	}
end

w:filter("tag", "value 2x 3x:absent")	-- filter the value is 2x and not 3x

for v in w:select "tag value:in" do
	print("2x", v.value)
end

w:filter("tag", "value 2x:absent 3x")	-- filter the value is 3x and not 2x

for v in w:select "tag value:in" do
	print("3x", v.value)
end

