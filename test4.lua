local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "a",
	type = "int",
}

w:register {
	name = "b",
	type = "float",
}

for i = 1, 20 do
	w:new { a = i }
	w:new { b = i }
end

for i = 20, 40 do
	w:new { a = i , b = i }
end

w:update()

for v in w:select "a:in" do
	if v.a % 2 == 1 then
		w:remove(v)
	end
end

for v in w:select "b:in" do
	if v.b < 10 then
		w:remove(v)
	end
end

for v in w:select "REMOVED a?in b?in" do
	print(v.a, v.b, "Removed")
end

w:update()

for v in w:select "a:in" do
	print(v.a)
end

for v in w:select "b:in" do
	print(v.b)
end

