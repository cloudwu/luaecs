-- test singleton
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "lsingleton",
	type = "lua",
}

w:register {
	name = "vsingleton",
	type = "int",
}

w:register {
	name = "msingleton",
	"x:float",
	"y:float",
}

w:new { lsingleton = "Init", vsingleton = 42, msingleton = { x = 1, y = 2 } }

w:update()

print(w:singleton "lsingleton")
w:singleton("lsingleton", "Hello World")
print(w:singleton "lsingleton")

print(w:singleton "vsingleton")
print(w:singleton("vsingleton", 100))
print(w:singleton("vsingleton"))

local v = w:singleton "msingleton"
print(v.x, v.y)

v.x, v.y = v.y, v.x
local v = w:singleton("msingleton" , v)
print(v.x, v.y)
