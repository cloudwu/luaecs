local ecs = require "ecs"

local function new_world()
	local w = ecs.world()

	w:register {
	  name = "value",
	  type = "int",
	}

	w:register {
		name = "tag"
	}

	return w
end


local w = new_world()

w:new {
  value = 1,
}

w:new {
  value = 2,
  tag = true,
}

local writer = ecs.writer "temp.bin"
writer:write(w, w:component_id "value")
writer:write(w, w:component_id "tag")
local meta = writer:close()
local function print_section(s)
	print("offset =", s.offset)
	print("stride =", s.stride)
	print("n = ", s.n)
end
print_section(meta[1])
print_section(meta[2])


local w = new_world()
local reader = ecs.reader "temp.bin"
w:read_component(reader, "value", meta[1].offset, meta[1].stride, meta[1].n)
w:read_component(reader, "tag", meta[2].offset, meta[2].stride, meta[2].n)
reader:close()

for v in w:select "value:in tag" do
	print(v.value)
end