local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "mark"
}

w:new {
	value = 1,
	mark = true,
}

w:new {
	mark = true,
}


for v in w:select "mark" do
	w:extend(v, "value?in")
	print("VALUE=", v.value)
end

for v in w:select "mark" do
	print(pcall(w.extend, w, v, "value:in"))
end

for v in w:select "value:in mark?in" do
	print(v.value, v.mark)
end

for v in w:select "mark value" do
	-- disable mark with value
	w:extend(v, "mark:out")
	v.mark = false
end

for v in w:select "value:in mark?in" do
	print(v.value, v.mark)
end