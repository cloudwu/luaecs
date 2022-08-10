-- group api
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "id",
	type = "int",
}

w:register {
	name = "group",
	type = "int"
}

w:register {
	name = "visible"
}

for i = 1, 100 do
	local eid = w:new {
		id = i,
		group = i % 5
	}
	w:group_add(i % 5, eid)
end

w:update()

for v in w:select "id:in group:in" do
	if v.id % 3 == 0 then
		print("REMOVE", v.id, v.group)
		w:remove(v)
	end
end

w:update()

-- enable all entities where group == 0 or group == 1
w:group_enable("visible", 0,2)

for v in w:select "visible id:in group:in" do
	print(v.id, v.group)
end

local eid = w:new { id = 42 }

w:group_add (1000, eid)
w:group_add (1001, eid)

w:group_enable("visible", 1000, 1001)

for v in w:select "visible id:in" do
	print(v.id)
end

local g = w:group_get(0)
for _, eid in ipairs(g) do
	print("GROUP0", eid)
end

-------------------------
local e = {}

-- Random apply group
for i = 1, 1000 do
	local eid = w:new()
	local group = math.random(2000, 2010)
	e[eid] = group
	w:group_add(group, eid)
end

-- Random remove
for k,v in pairs(e) do
	if math.random(1, 3) == 1 then
		w:remove(k)
		e[k] = nil
	end
end
w:update()

local function same_array(a, b)
	local n = #a
	assert(n == #b)
	for i = 1, n do
		assert(a[i] == b[i])
	end
end

w:register {
	name = "TEST"
}

local function insert_group(r, gid, ...)
	if gid == nil then
		return
	end
	for k,v in pairs(e) do
		if v == gid then
			table.insert(r, k)
		end
	end
	insert_group(r, ...)
end

local function merge_group(...)
	local r = {}
	insert_group(r, ...)
	table.sort(r)
	return r
end

local function test(...)
	local mg = merge_group(...)
	w:group_enable("TEST", ...)
	local r = {}
	for v in w:select "TEST eid:in" do
		table.insert(r, v.eid)
	end
	same_array(mg, r)
end

local function print_group(gid)
	local g = w:group_get(gid)
	print(table.concat(g, ","))
end

test(2000, 2001, 2005)