-- test object
local ecs = require "ecs"

local w = ecs.world()

w:register {
	name = "refobject",
	type = "int",
	ref = true,
}

local id1 = w:ref("refobject", { refobject = 42 })
local id2 = w:ref("refobject", { refobject = 0 })
local id3 = w:ref("refobject", { refobject = 100 })
print ("New", id1, id2, id3)
print(w:object("refobject", id1))

print("Release", id1)

w:release("refobject", id1)

for v in w:select "refobject:in" do
	print(v.refobject)
end

local id4 = w:ref("refobject", { refobject = -42 })
print ("New", id4)

print ("Release", id2)

w:release("refobject", id2)

print ("Release", id3)

w:release("refobject", id3)

print "List refobject"

for v in w:select "refobject:in" do
	print(v.refobject)
end

w:register {
	name = "index",
	type = "int",
}

w:new {
	index = id4
}

for v in w:select "index:in" do
	print(v.index)
end

print "Index refobject"

for v in w:select "refobject(index):in" do
	print(v.refobject)
end

w:register {
	name = "name",
	type = "lua",
}

w:new {
	name = "Hello"
}

for v in w:select "index:in" do
	print(v.index)
end


for v in w:select "name refobject(index):temp" do
	v.refobject = 42
end

for v in w:select "refobject:in" do
	print(v.refobject)
end

for v in w:select "refobject(index):update" do
	v.refobject = v.refobject + 1
end

for v in w:select "refobject:in" do
	print(v.refobject)
end

w:register {
	name = "mark"
}

w:sync("refobject mark?out", { id4 , mark=true })


w:ref ("refobject",  {
	refobject = 42,
	mark = true
})

print "Marked refobject"

for v in w:select "mark refobject?in" do
	print(v.refobject)
end
