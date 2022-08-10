-- readall for debug
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "vector",
	"x:float",
	"y:float",
}

w:register {
	name = "text",
	type = "lua",
}

w:register {
	name = "mark"
}

w:register {
	name = "index",
	type = "int",
}

w:new {
	vector = { x = 1, y = 2 },
	text = "Hello World",
	mark = true,
	index = 2,
}

w:new {
	text = "Hello World 2",
	mark = true,
	index = 1,
}

for v in w:select "mark" do
	w:readall(v)
	for k,v in pairs(v) do
		print(k,v)
	end
end

local ids = w:dumpid "index"

for idx, id in ipairs(ids) do
	print(idx, id)
end

local eid = w:dumpid "eid"

for idx, id in ipairs(ids) do
	print("EID", idx, id)
end
