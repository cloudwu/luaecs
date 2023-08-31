local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "value_backbuffer",
	type = "int",
}

w:register {
	name = "temp"
}

for i = 1, 8 do
	w:temporary("temp", "value", i)
end

w:swap("value", "value_backbuffer")

for v in w:select "value:in" do
	print("VALUE", v.value)
end

for v in w:select "value_backbuffer:in" do
	print("BACKBUFFER", v.value_backbuffer)
end

w:swap("value", "value_backbuffer")

for v in w:select "value:in" do
	print("VALUE", v.value)
end
