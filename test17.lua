-- Count select pattern
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int",
}

w:register {
	name = "tag2"
}

w:register {
	name = "tag3"
}

for i = 1, 100 do
	w:new { id = i }
end

for v in w:select "id:in tag2?out tag3?out" do
	if v.id % 2 == 0 then
		print("Mark Tag2", v.id)
		v.tag2 = true
	end
	if v.id % 3 == 0 then
		print("Mark Tag3", v.id)
		v.tag3 = true
	end
end

local count = w:count "tag2 tag3"

print("Count tag2 & tag3", count)


local n = 0
for v in w:select "tag2 tag3 id:in" do
	n = n + 1
end

assert(count == n)