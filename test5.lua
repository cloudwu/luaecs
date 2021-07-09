local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "a",
	type = "int",
}

w:register {
	name = "temp",
	type = "int",
}

for i = 1, 10 do
	w:new { a = i }
end

for v in w:select "a:in" do
	if v.a %2 == 0 then
		v.a = -v.a
		v.temp = 42
		w:sync("a:out temp:temp", v)
	end
end

for v in w:select "a:in temp?in" do
	print(v.a, v.temp)
end