local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "name",
	type = "lua",
}

w:register {
	name = "temp"
}

for i = 1, 10 do
	w:temporary("temp", "name", "id:" .. i)
end

for i = 1, 8 do
	w:temporary("temp", "value", i)
end

for v in w:select "name:in" do
	print(v.name)
end

for v in w:select "value:in" do
	print(v.value)
end

w:clear "name"

for i = 1, 8 do
	w:temporary("temp", "name", "new:" .. i)
end

for i = 1, 8 do
	w:temporary("temp", "value", i + 100)
end

for v in w:select "name:in" do
	print(v.name)
end

for v in w:select "value:in" do
	print(v.value)
end
