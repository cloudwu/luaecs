-- remove entities with tag

-- group api
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int64",
}

w:register {
	name = "tag",
}


for i = 1, 10 do
	w:new {
		id = i,
		tag = (i % 2 == 0)
	}
end

for v in w:select "tag id:in" do
	print (v.id)
end

for v in w:select "id:in tag:absent" do
	w:remove(v)
	print ("REMOVE", v.id)
end

w:remove_update "tag"	-- remove all the entities with tag

for v in w:select "id:in" do
	print (v.id)
end

for v in w:select "REMOVED id:in" do
	print ("REMOVED", v.id)
end

w:update()

assert((w:count "id") == 0)

