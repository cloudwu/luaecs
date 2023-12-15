local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "scene",
	"parent:int64",
}

w:register {
	name = "dummy",
}

w:register {
	name = "visible",
}

w:register {
	name = "name",
	type = "lua"
}

--[[
	A
	B
		C
			H
			I
	D
		E
			F
		G
		J
	DUMMY
]]

local eid = {}

local function new(v, parent)
	if parent then
		if parent == "" then
			v.scene = { parent = 0 }
		else
			local pid = assert(eid[parent])
			v.scene = { parent = pid }
		end
	end
	local id = w:new(v)
	if v.name then
		eid[v.name] = id
	end
end

new ( { name = "FIRST" }, "" )
new ( { name = "A" }, "" )
new ( { name = "B" , visible = true }, "" )
new ( { name = "C" }, "B" )
new ( { name = "D" }, "" )
new ( { name = "E" , visible = true }, "D" )
new ( { name = "F" }, "E" )
new ( { name = "G" }, "D" )
new ( { dummy = true , name = "dummy", visible = true } )	-- no scene
new ( { name = "H" }, "C" )
new ( { name = "I" }, "C" )
new ( { name = "J" }, "D" )
new ( { dummy = true , name = "dummy2"} )	-- no scene
new ( { dummy = true , name = "dummy3", visible = true } )	-- no scene

for v in w:select "name:in scene?in eid:in visible?in" do
	print(v.eid, v.name, v.scene and v.scene.parent, v.visible)
end

print "propagate"

w:propagate("scene", "visible")

for v in w:select "name:in scene?in eid:in visible" do
	print(v.eid, v.name, v.scene and v.scene.parent)
end


