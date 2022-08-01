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

local eid = w:new {
	value = 42
}

for i = 1, 10 do
	local str
	if i % 3 == 0 then
		str = "Str" .. i
	end
	w:new {
		value = i,
		string = str,
	}

end

for v in w:select "value:in eid:in" do
	if v.value % 2 == 0 then
		print(v.value, v.eid, "REMOVED")
		w:remove(v.eid)
	else
		-- read component by eid
		local str = w:access(v.eid, "string")
		print(v.value, str, v.eid)
	end
end

assert(w:exist(eid))
assert(w:access(eid, "REMOVED") == true)
assert(w:access(eid, "value") == 42)

w:update()

assert(w:exist(eid) == false)

for v in w:select "value:in string?in eid:in" do
	print(v.value, v.string, v.eid)
	if v.value % 2 == 1 then
		w:remove(v)
	end
end
