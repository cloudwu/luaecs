local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "v",
	type = "int",
}

w:register {
	name = "marked"
}

for i = 1, 10 do
	w:new {
		v = i,
		marked = i % 2 == 1,
	}
end

for v in w:select "v:in marked?in" do
	print(v.v, v.marked)
end

print "Marked"
for v in w:select "v:in marked" do
	print(v.v)
end

print "Not Marked"
for v in w:select "v:in marked:absent" do
	print(v.v)
end