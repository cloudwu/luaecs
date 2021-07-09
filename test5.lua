local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "a",
	type = "int",
}

for i = 1, 10 do
	w:new { a = i }
end

for v in w:select "a:in" do
	if v.a %2 == 0 then
		v.a = -v.a
		w:sync("a:out", v)
	end
end

for v in w:select "a:in" do
	print(v.a)
end