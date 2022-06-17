-- create entity from template
local ecs = require "ecs"

local w = ecs.world()

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
	type = "lua"
}

local function seri(v)
	assert(type(v) == "string")
	return v
end

local function deseri(v)
	return v
end

local t = w:template( {
	tag = true,
	value = 42,
	name = "noname",
} , seri)


w:template_instance(t, deseri , { id = 1 })
w:template_instance(t, deseri , { id = 2 })

for v in w:select "id:in value:in name:in" do
	print(v.id, v.value, v.name)
end
