-- test sort

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

w:register {
	name = "sort",
	order = true,
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

for v in w:select "sort data:in index:in" do
	print(v.data, v.index)
end

w:register {
	name = "sorted_index",
	type = "int",
}

for i = 1, 10 do
	w:new { data = i * 0.5 , sorted_index = i * 2 }
end

for v in w:select "sorted_index:in data?in" do
	print(v.sorted_index, "=>", v.data)
end

--w:remove(iter)
