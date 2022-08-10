-- create entity from template
local ecs = require "ecs"

local w = ecs.world()

local function seri(v)
	assert(type(v) == "string")
	return v
end

local function deseri(v)
	return v
end

w:register {
	name = "id",
	type = "int",
}

w:register {
	name = "value",
	type = "int",
}

w:register {
	name = "tag"
}

w:register {
	name = "name",
	type = "lua",
	marshal = seri,
	unmarshal = deseri,
}

w:new()	-- skip eid

local t = w:template {
	tag = true,
	value = 42,
	name = "noname",
}

w:template_instance(w:new(), t, { id = 1 })
w:template_instance(w:new(), t, { id = 2 })

for v in w:select "eid:in id:in value:in name:in" do
	print(v.eid, v.id, v.value, v.name)
end
