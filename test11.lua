local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "order",
	order = true,
}

w:register {
	name = "node",
	"id:int",
	"parent:int",
}

w:new {
	node = { id = 1, parent = 0 } ,
	order = true,
}

w:new {
	node = { id = 2, parent = 0 },
	order = true,
}

w:new {
	node = { id = 0, parent = -1 },
}

w:new {
	node = { id = 3, parent = 0 },
	order = true,
}

for v in w:select "node:in order?new" do
	assert(v.order == nil)
	if v.node.parent < 0 then
		-- add order
		v.order = true
	end
end

local cache = {}

for v in w:select "order node:update" do
	local node = v.node
	if node.parent < 0 or cache[node.parent] then
		cache[node.id] = true
		print(node.id, node.parent)
	else
		v.order = false
	end
end

assert(pcall(w.select, w, "node order") == false)
