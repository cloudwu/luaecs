local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "string",
	type = "lua",
}

w:register {
	name = "tag"
}

local eid = w:new {
	tag = true
}

local eid2 = w:new()

w:import(eid2, {
	string = "Hello2",
	tag = true,
})

w:import(eid, {
	string = "Hello",
	tag = false,
})

assert(w:access(eid, "string") == "Hello")
assert(w:access(eid, "tag") == false)

assert(w:access(eid2, "string") == "Hello2")
assert(w:access(eid2, "tag") == true)

local s = {}

for v in w:select "string:in" do
	table.insert(s, v.string)
end

assert(s[1] == "Hello")
assert(s[2] == "Hello2")
