local ecs = require "ecs"

local ref1 = ecs.ref {
	"x:float",
	"y:float",
}

local ref2 = ecs.ref { type = "lua" }

local ref3 = ecs.ref { type = "int" }


local id = ref1:new { x = 1, y = 2 }
local v = ref1[id]
v.x, v.y = v.y , v.x
ref1[id] = v
print(v.x, v.y)

local id1 = ref2:new "Hello"
local id2 = ref2:new "World"
print(ref2[id1], ref2[id2])
ref2:delete(id1)
print(ref2[id1], ref2[id2])

local id = ref3:new(100)
print(ref3[id])
ref3[id] = 42
print(ref3[id])
ref3:delete(id)

local test = require "ecs.ctest"
local id = test.testref(ref1)
local v = ref1[id]
print(v.x, v.y)
