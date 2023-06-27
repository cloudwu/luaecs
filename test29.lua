local ecs = require "ecs"
local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

for i = 1, 10 do
	w:new { value = i }
end

for i = 1, 10 do
	print(w:object("value", i))
end

for i = 1, 10 do
	w:object("value", i, 10 - i)
end

for i = 1, 10 do
	print(w:object("value", i))
end
