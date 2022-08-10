-- group api
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int",
}

w:register {
	name = "group",
	type = "int"
}

w:register {
	name = "visible"
}

for i = 1, 100 do
	local eid = w:new {
		id = i,
		group = i % 5
	}
	w:group_add(i % 5, eid)
end

w:update()

for v in w:select "id:in group:in" do
	if v.id % 3 == 0 then
		print("REMOVE", v.id, v.group)
		w:remove(v)
	end
end

w:update()

-- enable all entities where group == 0 or group == 1
w:group_enable("visible", 0,2)

for v in w:select "visible id:in group:in" do
	print(v.id, v.group)
end

local eid = w:new { id = 42 }

w:group_add (1000, eid)
w:group_add (1001, eid)

w:group_enable("visible", 1000, 1001)

for v in w:select "visible id:in" do
	print(v.id)
end

local g = w:group_get(0)
for _, eid in ipairs(g) do
	print("GROUP0", eid)
end