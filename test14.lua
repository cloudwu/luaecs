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


for v in w:select "value:in eid:in tag" do
	print(v.value, v.eid)
end


local writer = ecs.writer "temp.bin"
writer:write(w, w:component_id "eid")
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
print_section(meta[3])

w:clearall()
local reader = ecs.reader "temp.bin"
local maxid = w:read_component(reader, "eid", meta[1].offset, meta[1].stride, meta[1].n)
local value_n = w:read_component(reader, "value", meta[2].offset, meta[2].stride, meta[2].n)
local tag_n = w:read_component(reader, "tag", meta[3].offset, meta[3].stride, meta[3].n)
reader:close()

assert(maxid >= value_n)
assert(maxid >= tag_n)

w:new {
  value = 3,
  tag = true,
}

for v in w:select "value:in eid:in tag" do
	print(v.value, v.eid)
end

-- You can use generate_eid instead of reading eid from file
w:generate_eid()

for v in w:select "value:in eid:in tag" do
	print(v.value, v.eid)
end
