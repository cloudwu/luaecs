local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "tag",
}

local obj = {}
for i = 1, 10 do
	local eid = w:new { tag = (i % 2 == 0) }
	obj[i] = eid
end

print "==== 1 ===="

for v in w:select "tag eid:in" do
	if v.eid == 4 then
		w:access(1, "tag", true)
	end
	print(v.eid)
end

print "==== 2 ===="

for v in w:select "tag eid:in" do
	if v.eid == 4 then
		w:access(1, "tag", false)
		w:access(6, "tag", false)
		w:access(7, "tag", true)
	end
	print(v.eid)
end

print "==== 3 ===="

for v in w:select "tag eid:in" do
	print(v.eid)
end