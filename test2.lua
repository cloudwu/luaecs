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
	w:sync("value?in", v)
	print(v.value)
end

for v in w:select "mark" do
	print(pcall(w.sync, w, "value:in", v))
end

for v in w:select "value:in mark?in" do
	print(v.value, v.mark)
end

for v in w:select "mark value" do
	-- disable mark with value
	v.mark = false
	w:sync("mark:out", v)
end

for v in w:select "value:in mark?in" do
	print(v.value, v.mark)
end