local ecs = require "ecs"
local w = ecs.world()

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "name",
	type = "lua",
}

local eid = w:new { value = 42, name = "" }

local e = w:fetch(eid)
w:extend(e, "value:in name:out")
e.name = "Name:"..e.value
w:submit(e)

for v in w:select "value:in name:in" do
	print(v.value, v.name)
end


