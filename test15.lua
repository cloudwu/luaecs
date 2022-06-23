-- group api
local ecs = require "ecs"

local w = ecs.world()

w:register {
  name = "id",
  type = "int64",
}

w:register {
	name = "visible"
}

w:group_init "group"


for i = 1, 100 do
	w:new {
		id = i,
		group = i%4,
	}
end

w:group_update()
w:update()

for v in w:select "id:in" do
	if v.id % 10 == 0 then
		print("REMOVE", v.id)
		w:remove(v)
	end
end

w:update()

w:group_check()
local g = w:group_fetch(0)
for _,uid in ipairs(g) do
	print("GROUP0", uid)
end

-- enable all entities where group == 0 or group == 1
w:group_enable("visible", 0,1)

for v in w:select "visible id:in" do
	print(v.id)
end

