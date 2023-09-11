package.cpath = "./?.dll"
local ecs_core = require "ecs.core"
ecs_core.DEBUG = true	-- turn on DEBUG

local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "size",
	"x:int",
	"y:int",
}

w:new {
	size = {
		x = 42,
		y = 24,
	}
}

local function foo()
	for v in w:select "size:in" do
		v.size.x = 0
	end
end

local v = w:first "size:in"
assert(v.size.x == 42)

print(pcall(foo))



