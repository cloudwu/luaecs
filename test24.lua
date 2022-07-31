package.cpath = "./?.dll"
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "string",
	type = "lua",
}

w:new {
	value = 42
}

for i = 2, 10 do
	local str
	if i % 3 == 0 then
		str = "Str" .. i
	end
	w:new {
		value = i,
		string = str,
	}

end

for v in w:select "value:in string?in _eid:in" do
	if v.value % 2 == 0 then
		print(v.value, v.string, v._eid, "REMOVED")
		w:remove(v)
	else
		print(v.value, v.string, v._eid)
	end
end

w:update()

print "------"

for v in w:select "value:in string?in _eid:in" do
	print(v.value, v.string, v._eid)
	if v.value % 2 == 1 then
		w:remove(v)
	end
end