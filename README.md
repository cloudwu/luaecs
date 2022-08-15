Lua ECS
=======

A simple Entity-Componet library for Lua.

```Lua
-- Create a world
local w = ecs.world()

-- Register a new type "vector( x:float, y:float )" into world.
-- It's a C component (The data is in C memory).
w:register {
	name = "vector",
	"x:float",
	"y:float",
}

-- Register a new type "name" into world.
-- It's a Lua component (The value can be any lua object)
w:register {
	name = "name",
	type = "lua",
}

-- Create a new entity with components vector and name.
local eid = w:new {
	vector = { 1.0, 2.0 },
	name = "point",
}

-- Iterate every entity with component vector and name
for v in w:select "vector:in name:in" do
	-- v is the iterator, we can read compionent from it.
	-- DO NOT store v outside of the iteration
	print(v.name)	-- output : point
	print(v.vector.x, v.vector.y)	-- output: 1.0, 2.0
end
```

Component Types
===============

> C Component

The component is stored in C side, it's compat and can be accessed from C side easily. The supported buildin types are:

*	int  (32bits)
*	float
*	bool (8bits)
*	int64
*	dword (unsigned 32bits)
*	word (unsigned 16bits)
*	byte (unsigned 8bits)
*	double
*	userdata (void *)

```lua
w:register {
	name = "vector",
	"x:float",
	"y:float",
}
```
The equivalent C struct is
```C
struct vector {
	float x;
	float y;
};
```
A single buildin C type can be register as
```lua
w:register {
	name = "visible",
	type = "bool",
}
```

> Lua component

The component is stored in Lua side , and can be any lua types, such as string, table, userdata, etc.
```lua
w:register {
	name = "name",
	type = "lua",
}
```
> Tag

The tag is a special component , it can be read as a boolean type, and the value must be true. It used for selection.
```lua
w:register {
	name = "visible"
}
```

> Entity ID

Each entity has a build-in readonly component `eid` , it's a 64bits unique monotonic ID. The newest entity always has the biggest eid.

Select Pattern
====

Use `for v in world:select(pattern)` to manage components. It iterate all the entities in the world and filter out the entities matching the pattern. The order of iteration is the creation order of the entities.
```lua
-- print all the entities' eid
for v in w:select "eid:in" do
	print(v.eid)
end
```

The pattern is a space-separated combination of `componentname[:?]action`, and the `action` can be
* in  : read the component
* out : write the component
* update : read / write
* absent : check if the component is not exist
* exist  (default) : check if the component is exist
* new : create the component
* ? means it's an optional action if the component is not exist
```lua
-- clear temp component from all entities
w:clear "temp"

-- Iterate the entity with visible/value/output components and without readonly component
-- Create temp component for each.
for v in w:select "visible readonly:absent value:in output:out temp:new" do
	v.output = v.value + 1
	v.temp = v.value
end
```
NOTICE: If you use action `new` , you must guarantee the component is clear (None entity has this component) before iteration. 

Create Entity
====
```lua
-- create an empty entity
local eid = w:new()
-- Import component "name" into entity (eid)
w:import(eid, { name = "foobar" })
-- Or you can use
local eid = w:new { name = "foobar" }
```
You can also create an entity from a template :
```lua
-- Create a template first
local t = w:template {
	name = "foobar"
}
-- instance the template into an entity, and add visible tag.
-- The additional components ( { visible = true } ) is optional.
local eid = w:template_instance(w:new(), t, { visible = true })
```
You can offer  `init` / `marshal` / `unmarshal` functions for the component type in `w:register()`.

`marshal` and `unmarshal` is for lua component of template, and `init` is the  constructor of the component, the common use is for C component initialization.

NOTICE: C component can be initialize by a lua string, you can use `string.pack()` to generate the C structure data.

Remove Entity
====

```lua
-- Remove an entity with eid
w:remove(eid)
-- Or remove an entity with an iterator
for v in w:select "value:in" do
	if v.value == 42 then
		w:remove(v)
	end
end

for v in w:select "REMOVED value:in do
	assert(v.value == 42)
end

-- delete all the entities removed
w:update()
```

The entities removed are not going to disppear immediately, they are tagged with `REMOVED` only, and deleted after you call `w:update()`.

Group
=====
You can add an entity into a group after creation. Each entity can belongs one or more groups (or no group).

```lua
-- Add entity (eid) into a group with groupid (32bit integer)
w:group_add(groupid, eid)
```

You can tags entities in groups with `w:group_enable(groupid1, groupid2,...)`

Persistance
=====
Only C components can be persistance.

```lua
-- Save
-- Create a writer to file "saves.bin"
local writer = ecs.writer "saves.bin"
writer:write(w, w:component_id "eid") -- write component eid
writer:write(w, w:component_id "value") -- write component value
writer:write(w, w:component_id "tag") -- write component tag
local meta = writer:close()
local function print_section(s)
	print("offset =", s.offset)
	print("stride =", s.stride)
	print("n = ", s.n)
end
print_section(meta[1])	-- meta information of eid
print_section(meta[2])	-- meta information of value
print_section(meta[3])	-- meta information of tag
```

```lua
-- Load
local reader = ecs.reader "saves.bin"
local maxid = w:read_component(reader, "eid", meta[1].offset, meta[1].stride, meta[1].n)
local value_n = w:read_component(reader, "value", meta[2].offset, meta[2].stride, meta[2].n)
local tag_n = w:read_component(reader, "tag", meta[3].offset, meta[3].stride, meta[3].n)
reader:close()
```

You can also use `w:generate_eid()` instead of reading eid from file

Other APIs
=====

> w:exist(eid)	-- Check if the entity with eid exist

> w:clear(typename) -- delete component (typename) from all the entities

>  w:clearall() -- delete all the entities

> w:component_id(typename) -- returns the component id of the component (typename) . It's for C systems.

> w:type(typename) -- returns "tag", "lua" or "c"

> w:first(pattern) -- Read the first component with the pattern.

> w:filter(tagname, pattern) -- Enable tags marching the pattern

Access Components from C side
======

1. Create a context by `w:context { component1, component2, ... }` for C. Export the components C concerned.
2. Define C structs of components in C.
3. Use APIs in `luaecs.h`

> `void * entity_iter(struct ecs_context *ctx, cid_t cid, int index)`
> `void * entity_sibling(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id)`

```C
struct component *v;
int i;
// iterate all the components with COMPONENT_ID. COMPONENT_ID is the index of context (base 0) or component id from lua MAKE_COMPONENT_ID(cid)
for (i = 0; (v = (struct component *)entity_iter(ctx, COMPONENT_ID, i)); i++) {
	// Read the component2 associate with component (the same entity)
	struct component2 * c2 = (struct component2 *)entity_sibling(ctx, COMPONENT_ID, i, COMPONENT_ID2);
}
```

> `int entity_sibling_id(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id)`

The same with entity_sibling, but returns an id (base 1). 0 : not exist.

> `void * entity_add_sibling(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id, const void *buffer)`

> `int entity_new(struct ecs_context *ctx, cid_t cid, const void *buffer)`

Create an entity with one component

> `void entity_remove(struct ecs_context *ctx, cid_t cid, int index)`

> `void entity_enable_tag(struct ecs_context *ctx, cid_t cid, int index, cid_t tag_id)`

> `void entity_disable_tag(struct ecs_context *ctx, cid_t cid, int index, cid_t tag_id)`

> `int entity_get_lua(struct ecs_context *ctx, cid_t cid, int index, void *L)`
> `int entity_sibling_lua(struct ecs_context *ctx, cid_t cid, int index, cid_t sibling_id, void *L)`

> `void entity_group_enable(struct ecs_context *ctx, int tagid, int n, int groupid[])`
