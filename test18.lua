-- entity reborn

-- group api
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int64",
}

w:register {
	name = "name",
	type = "lua",
}

w:register {
	name = "visible"
}

w:register {
	name = "tag"
}

w:group_init "group"


w:new {
	id = 0,
	name = "zero",
	group = 0,
	tag = true,
}

w:new {
	id = 1,
	name = "one",
	group = 0,
	tag = true,
}

w:group_update()
w:update()

w:group_enable("visible", 0)

for v in w:select "visible id:in name:in tag?in" do
	print("GROUP 0 : ",v.id, v.name, v.tag)
	if v.id == 0 then
		w:remove(v)	-- remove 0
	end
end

w:group_update()
w:update()

w:group_enable("visible", 1)

for v in w:select "visible id:in name:in tag?in" do
	print("GROUP 1 : ", v.id, v.name, v.tag)
end





