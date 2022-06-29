local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int64",
}

w:register {
	name = "tag",
}

w:register {
	name = "text",
	type = "lua",
}

w:register {
	name = "cstruct",
	"x:int",
	"y:int"
}

assert(w:type "id" == "int64")
assert(w:type "tag" == "tag")
assert(w:type "text" == "lua")
assert(w:type "cstruct" == "c")
