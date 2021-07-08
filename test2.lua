local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "data",
	type = "float",
}

w:register {
	name = "index",
	type = "int",
}

local tmp = { 10,9,8,7,6,5,4,3,2,1 }

for i = 1, 10, 2 do
	w:new { data = tmp[i], index = tmp[i] }
	w:new { index = tmp[i+1] }
end

w:update()

for v in w:select "index data?in" do
	print(v.data)
end

w:sort("sort", "index")

print "sorted"

for v in w:select "sort:in data:in index:in" do
	print(v.data, v.index)
end
