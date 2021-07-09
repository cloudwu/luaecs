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
	if v.a == 5 then
		v.a = 42
		w:sync("a:out", v)
		break
	end
end

for v in w:select "a:in" do
	print(v.a)
end