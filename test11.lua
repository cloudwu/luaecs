local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "order",
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
	node = { id = 0, parent = -1 },
	order = true,
}

local cache = {}

w:order_iterate("order", function(self, v)
	self:sync("node:in", v)
	local node = v.node
	if node.parent < 0 or cache[node.parent] then
		cache[node.id] = true
		print(node.id, node.parent)
		return
	end
	return true	-- yield
end)


