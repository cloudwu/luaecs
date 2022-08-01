local ecs = require "ecs.core"

local rawerror = error
local selfsource <const> = debug.getinfo(1, "S").source
local function error(errmsg)
	local level = 2
	while true do
		local info = debug.getinfo(level, "S")
		if not info then
			rawerror(errmsg, 2)
			return
		end
		if selfsource ~= info.source then
			rawerror(errmsg, level)
			return
		end
		level = level + 1
	end
end

local function assert(cond, errmsg)
	if not cond then
		error(errmsg or "assertion failed!")
	end
	return cond
end

local function get_attrib(opt, inout)
	if opt == nil then
		return { exist = true }
	end
	local desc = {}
	if opt == "?" then
		desc.opt = true
	else
		assert(opt == ":")
	end
	if inout == "in" then
		desc.r = true
	elseif inout == "out" then
		desc.w = true
	elseif inout == "update" then
		desc.r = true
		desc.w = true
	elseif inout == "exist" then
		desc.exist = true
		assert(not desc.opt)
	elseif inout == "absent" then
		desc.absent = true
		assert(not desc.opt)
	else
		assert(inout == "new")
	end
	return desc
end

local persistence_methods = ecs._persistence_methods()
ecs.writer = persistence_methods.writer
ecs.reader = persistence_methods.reader

local function cache_world(obj, k)
	local c = {
		typenames = {},
		typeidtoname = {},
		id = 0,
		select = {},
		ref = {},
	}

	local function gen_ref_pat(key)
		local typenames = c.typenames
		local desc = {}
		local tc = typenames[key]
		if tc == nil then
			error("Unknown type " .. key)
		end
		local a = {
			exist = true,
			name = tc.name,
			id = tc.id,
			type = tc.type,
		}
		local n = #tc
		for i=1,#tc do
			a[i] = tc[i]
		end
		desc[1] = a
		return desc
	end

	local function gen_all_pat()
		local desc = {}
		local i = 1
		for name,t in pairs(c.typenames) do
			local a = {
				name = t.name,
				id = t.id,
				type = t.type,
				opt = true,
				r = true,
			}
			table.move(t, 1, #t, 1, a)
			desc[i] = a
			i = i + 1
		end
		return desc
	end

	setmetatable(c, { __index = function(_, key)
		if key == "all" then
			local all = k:_groupiter(gen_all_pat())
			c.all = all
			return all
		end
	end })

	local function gen_select_pat(pat)
		local typenames = c.typenames
		local desc = {}
		local idx = 1
		for token in pat:gmatch "[^ ]+" do
			local key, padding = token:match "^([_%w]+)(.*)"
			assert(key, "Invalid pattern")
			local opt, inout
			if padding ~= "" then
				opt, inout = padding:match "^([:?])(%l+)$"
				assert(opt, "Invalid pattern")
			end
			local tc = typenames[key]
			if tc == nil then
				error("Unknown type " .. key)
			end
			local a = get_attrib(opt, inout)
			if tc.raw then
				assert(not a.r and not a.w, "try to access raw component")
			end
			a.name = tc.name
			a.id = tc.id
			a.type = tc.type
			local n = #tc
			for i=1,#tc do
				a[i] = tc[i]
			end
			desc[idx] = a
			idx = idx + 1
		end
		return desc
	end

	local function cache_select(cache, pat)
		local pat_desc = gen_select_pat(pat)
		cache[pat] = k:_groupiter(pat_desc)
		return cache[pat]
	end

	setmetatable(c.select, {
		__mode = "kv",
		__index = cache_select,
		})

	local function cache_ref(cache, pat)
		local pat_desc = gen_ref_pat(pat)
		cache[pat] = k:_groupiter(pat_desc)
		return cache[pat]
	end

	setmetatable(c.ref, {
		__mode = "kv",
		__index = cache_ref,
		})

	obj[k] = c
	return c
end

local context = setmetatable({}, { __index = cache_world })
local typeid = {
	int = assert(ecs._TYPEINT),
	float = assert(ecs._TYPEFLOAT),
	bool = assert(ecs._TYPEBOOL),
	int64 = assert(ecs._TYPEINT64),
	dword = assert(ecs._TYPEDWORD),
	word = assert(ecs._TYPEWORD),
	byte = assert(ecs._TYPEBYTE),
	double = assert(ecs._TYPEDOUBLE),
	userdata = assert(ecs._TYPEUSERDATA),
}

local typesize = {
	[typeid.int] = 4,
	[typeid.float] = 4,
	[typeid.bool] = 1,
	[typeid.int64] = 8,
	[typeid.dword] = 4,
	[typeid.word] = 2,
	[typeid.byte] = 1,
	[typeid.double] = 8,
	[typeid.userdata] = 8,
}

local TYPENAME = {
	[typeid.int] = "int",
	[typeid.float] = "float",
	[typeid.bool] = "bool",
	[typeid.int64] = "int64",
	[typeid.dword] = "dword",
	[typeid.word] = "word",
	[typeid.byte] = "byte",
	[typeid.double] = "double",
	[typeid.userdata] = "userdata",
}

-- make metatable
local group_methods = ecs._group_methods()
local M = ecs._methods()
M.__index = M

do	-- newtype
	local function parse(s)
		-- s is "name:typename"
		local name, typename = s:match "^([%w_]+):(%w+)$"
		local typeid = assert(typeid[typename])
		return { typeid, name }
	end

	local function align(c, field)
		local t = field[1]
		local tsize = typesize[t]
		local offset = ((c.size + tsize - 1) & ~(tsize-1))
		c.size = offset + tsize
		field[3] = offset
		return field
	end

	local function align_struct(c)
		local max_align = 0
		for k,v in ipairs(c) do
			local s = typesize[v[1]]
			if s > max_align then
				max_align = s
			end
		end
		if max_align > 1 then
			max_align = max_align - 1
			c.size = ((c.size + max_align) & ~max_align)
		end
	end

	function M:register(typeclass)
		local name = assert(typeclass.name)
		local ctx = context[self]
		ctx.all = nil	-- clear all pattern
		local typenames = ctx.typenames
		local id = ctx.id + 1
		assert(typenames[name] == nil and id <= ecs._MAXTYPE)
		ctx.id = id
		local c = {
			id = id,
			name = name,
			size = 0,
			init = typeclass.init,
			marshal = typeclass.marshal,
			unmarshal = typeclass.unmarshal,
		}
		for i, v in ipairs(typeclass) do
			c[i] = align(c, parse(v))
		end
		local ttype = typeclass.type
		if ttype == "lua" then
			assert(c.size == 0)
			c.size = ecs._LUAOBJECT
			assert(c[1] == nil)
		elseif ttype == "raw" then
			assert(c.size == 0)
			c.size = typeclass.size
			c.raw = true
		elseif c.size > 0 then
			align_struct(c, c[1][1])
		else
			-- size == 0, one value
			if ttype then
				local t = assert(typeid[typeclass.type])
				c.type = t
				c.size = typesize[t]
				c[1] = { t, "v", 0 }
			else
				c.tag = true
			end
		end
		typenames[name] = c
		ctx.typeidtoname[id] = name
		self:_newtype(id, c.size)
	end
end

local function dump(obj)
	for e,v in pairs(obj) do
		if type(v) == "table" then
			for k,v in pairs(v) do
				print(e,k,v)
			end
		else
			print(e,v)
		end
	end
end

local function _new_entity(self, eid, obj)
	local typenames = context[self].typenames
	for k,v in pairs(obj) do
		local tc = typenames[k]
		if not tc then
			error ("Invalid key : ".. k)
		end
		local id = self:_addcomponent(eid, tc.id)
		local init = tc.init
		if init then
			v = init(v)
		end
		self:object(k, id, v)
	end
end

function M:new(obj, eid)
--	dump(obj)
	local index
	if eid then
		index = self:_indexentity(eid)
	else
		eid, index = self:_newentity()
	end
	if obj then
		_new_entity(self, index, obj)
	end
	return eid
end

local template_methods = ecs._template_methods()
function M:template_instance(temp, obj)
	local eid = self:_newentity()
	local ctx = context[self]
	local offset = 0
	local cid, arg1, arg2
	while true do
		cid, offset, arg1, arg2 = template_methods._template_extract(temp, offset)
		if not cid then
			break
		end
		local id = self:_addcomponent(eid, cid)
		local tname = ctx.typeidtoname[cid]
		local tc = ctx.typenames[tname]
		if tc.unmarshal then
			local v = tc.unmarshal( arg1, arg2 )
			if template_methods._template_instance_component(self, cid, id, v) then
				self:object(tname, id, v)
			end
		elseif not tc.tag then
			assert(tc.size > 0, "Missing unmarshal function for lua object")
			template_methods._template_instance_component(self, cid, id, arg1, arg2)
		end
	end
	if obj then
		_new_entity(self, eid, obj)
	end
end

function M:context(t)
	local typenames = context[self].typenames
	local id = {}
	for i, name in ipairs(t) do
		local tc = typenames[name]
		if not tc then
			error ("Invalid component name " .. name)
		end
		id[i] = tc.id
	end
	return self:_context(id)
end

function M:select(pat)
	return context[self].select[pat]()
end

do
	local access = M._access
	function M:access(eid, pat, ...)
		return access(self, eid, context[self].select[pat], ...)
	end
end

do
	local _count = M._count
	function M:count(pat)
		return _count(context[self].select[pat])
	end
end

do
	local EID <const> = ecs._EID
	local get_index = M._indexentity

	function M:fetch(eid, iter)
		iter = iter or {}
		iter[1] = get_index(self, eid) + 1
		iter[2] = EID
		return iter
	end
end

function M:sync(pat, iter)
	local p = context[self].select[pat]
	self:_sync(p, iter)
	return iter
end

function M:readall(iter)
	local p = context[self].all
	self:_sync(p, iter)
	return iter
end

function M:readid(iter)
	local p = context[self].all
	return self:_readid(p, iter)
end

function M:clear(name)
	local id = assert(context[self].typenames[name].id)
	self:_clear(id)
end

function M:clearall()
	for _, tc in pairs(context[self].typenames) do
		local id = assert(tc.id)
		if id >= 0 then
			self:_clear(id)
		end
	end
end

function M:dumpid(name)
	local typenames = context[self].typenames
	return self:_dumpid(typenames[name].id)
end

function M:component_id(name)
	local t = assert(context[self].typenames[name])
	return t.id
end

function M:read_component(reader, name, offset, stride, n)
	local t = assert(context[self].typenames[name])
	return persistence_methods._readcomponent(self, reader, t.id, offset, stride, n)
end

M.generate_eid = persistence_methods.generate_eid

do
	local _object = M._object
	function M:object(name, refid, v)
		local pat = context[self].ref[name]
		return _object(pat, v, refid)
	end

	function M:singleton(name, pattern, iter)
		local typenames = context[self].typenames
		if iter == nil then
			iter = { 1, typenames[name].id }
			if pattern then
				local p = context[self].select[pattern]
				return self:_read(p, iter)
			else
				return iter
			end
		else
			iter[1] = 1
			iter[2] = typenames[name].id
			local p = context[self].select[pattern]
			self:_sync(p, iter)
		end
		return iter
	end
end

do
	local _serialize = template_methods._serialize
	local _serialize_lua = template_methods._serialize_lua

	function M:template(obj, serifunc)
		local buf = {}
		local i = 1
		local typenames = context[self].typenames
		for k,v in pairs(obj) do
			local tc = typenames[k]
			if not tc then
				error ("Invalid key : ".. k)
			end
			buf[i] = tc.id
			if tc.marshal then
				buf[i+1] = _serialize_lua(tc.marshal(v))
			elseif tc.tag and v then
				buf[i+1] = "\0"
			else
				assert(tc.size > 0, "Missing marshal function for lua object")
				local pat = context[self].ref[k]
				buf[i+1] = _serialize(pat, v)
			end
			i = i + 2
		end
		return template_methods._template_create(self, buf)
	end
end

function M:group_init(groupname)
	self:register {
		name = groupname,
		type = "int",
	}
	local gsname = groupname .. "_"
	self:register {
		name = gsname,
		"uid:int64",
		"lastid:int64",
		"group:int",
		"next:int",
	}
	local ctx = context[self]
	ctx.group_id = ctx.typenames[groupname].id
	ctx.group_struct = ctx.typenames[gsname].id
	ctx.uid = 0
	ctx.group = {}
end

function M:group_id(iter)
	local ctx = context[self]
	return group_methods._group_id(self, iter,ctx.group_struct)
end

-- debug use
function M:group_fetch(groupid)
	local ctx = context[self]
	return group_methods._group_fetch(self, ctx.group, ctx.group_struct, groupid)
end

function M:group_check()
	local ctx = context[self]
	for k in pairs(ctx.group) do
		assert(	#group_methods._group_fetch(self, ctx.group, ctx.group_struct, k, true) ==
			#group_methods._group_fetch(self, ctx.group, ctx.group_struct, k, false) )
	end
end

function M:group_update()
	local ctx = context[self]
	ctx.uid = group_methods._group_update(self, ctx.group, ctx.group_id, ctx.group_struct, ctx.uid)
end

function M:group_enable(tagname, ...)
	local ctx = context[self]
	local tagid = ctx.typenames[tagname].id
	group_methods._group_enable(self, ctx.group, ctx.group_struct,tagid,...)
end

function M:remove_update(tagname)
	local t = assert(context[self].typenames[tagname])
	self:update(t.id)
end

function M:type(name)
	local t = context[self].typenames[name]
	if t.type then
		return TYPENAME[t.type]
	elseif t.tag then
		return "tag"
	elseif t.size == ecs._LUAOBJECT then
		return "lua"
	else
		return "c"
	end
end

do
	local cfilter = M._filter
	function M:filter(tagname, pat)
		local ctx = context[self]
		return cfilter(self, ctx.typenames[tagname].id, ctx.select[pat])
	end
end

function ecs.world()
	local w = ecs._world(M)
	context[w].typenames.REMOVED = {
		name = "REMOVED",
		id = ecs._REMOVED,
		size = 0,
		tag = true,
	}
	context[w].typenames.eid = {
		name = "eid",
		id = ecs._EID,
		size = 0,
		tag = true,
	}
	return w
end

return ecs
