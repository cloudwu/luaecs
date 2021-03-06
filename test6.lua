local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "t",
	"a:bool",
	"b:userdata",
}

w:register {
	name = "flag",
}

w:new {
	t = {
		a = false,
		b = ecs.NULL,
	}
}

local function print_v()
	local v = w:singleton("t", "t:in")
	print(".a = ",v.t.a)
	print(".b = ",v.t.b)
	local v = w:singleton "t"
	w:sync("t:in", v)
	print(".a = ", v.t.a)
	print(".a = ", v.t.b)
end

local ctx = w:context { "t" }

print("ctx = ", ctx)

local test = require "ecs.ctest"

print_v()

test.testuserdata(ctx)

print_v()

local v = w:singleton("t", "flag t:in")
assert(v == nil)

-- remove singleton of t
local v = w:singleton "t"
w:remove(v)
w:update()


w:new {
	t = {
		a = true,
		b = ecs.NULL,
	},
	flag = true,
}

local v = w:singleton("t", "flag t:in")
assert(v.t.a == true)
