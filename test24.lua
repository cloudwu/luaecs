package.cpath = "./?.dll"
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:new {
	value = 42
}

for i = 1, 10 do
	w:new {
		value = i
	}
end

for v in w:select "value:in _eid:in" do
	print(v.value, v._eid)
	if v.value % 2 == 1 then
		w:remove(v)
	end
end

w:update()

for v in w:select "value:in _eid:in" do
	print(v.value, v._eid)
	if v.value == 5 then
		w:remove(v)
	end
end


