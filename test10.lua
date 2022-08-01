local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "key",
	type = "int",
}

w:register {
	name = "temp",
	type = "lua",
}

for i = 1, 10 do
  w:new { key = i }
end

local function add_temp(i)
	for v in w:select "key:in temp?new" do
		if v.key == i then
			v.temp = "Temp"
		end
	end
end

local function print_entity()
	for v in w:select "key:in temp:in" do
		print(v.key, v.temp)
	end
end

add_temp(5)
print_entity()

add_temp(6)
print_entity()
