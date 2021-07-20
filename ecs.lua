local ecs = require "ecs.core"

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
	elseif inout == "out" or inout == "new" then
		desc.w = true
	elseif inout == "update" then
		desc.r = true
		desc.w = true
	elseif inout == "exist" then
		desc.exist = true
		assert(not desc.opt)
	else
		assert(inout == "temp")
	end
	return desc
end

local function cache_world(obj, k)
	local c = {
		typenames = {},
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

	local function gen_select_pat(pat)
		local typenames = c.typenames
		local desc = {}
		local idx = 1
		for token in pat:gmatch "[^ ]+" do
			local key, index, padding = token:match "^([_%w]+)%(?([_%w]*)%)?(.*)"
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
			if index ~= "" then
				local indexc = typenames[index]
				if indexc == nil then
					error("Unknown index type "..index)
				end
				local a = get_attrib(opt, inout == "temp" and "temp" or "in")
				a.name = index
				a.id = indexc.id
				a.type = indexc.type
				a.ref = true
				desc[idx] = a
				idx = idx + 1
			elseif tc.ref and inout ~= "new" then
				local live = typenames[key .. "_live"]
				local a = {
					exist = true,
					name = live.name,
					id = live.id,
				}
				desc[idx] = a
				idx = idx + 1
			end
			local a = get_attrib(opt, inout)
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

local M = ecs._METHODS

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

	local function align_struct(c, t)
		if t then
			local s = typesize[t] - 1
			c.size = ((c.size + s) & ~s)
		end
	end

	function M:register(typeclass)
		local name = assert(typeclass.name)
		local ctx = context[self]
		local typenames = ctx.typenames
		local id = ctx.id + 1
		assert(typenames[name] == nil and id <= ecs._MAXTYPE)
		ctx.id = id
		local c = {
			id = id,
			name = name,
			size = 0,
		}
		for i, v in ipairs(typeclass) do
			c[i] = align(c, parse(v))
		end
		local ttype = typeclass.type
		if ttype == "lua" then
			assert(c.size == 0)
			c.size = ecs._LUAOBJECT
			c.islua = true
		elseif c.size > 0 then
			align_struct(c, typeclass[1][1])
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
		self:_newtype(id, c.size)
		if typeclass.ref then
			c.ref = true
			self:register { name = name .. "_live" }
			self:register { name = name .. "_dead" }
		end
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

function M:new(obj)
--	dump(obj)
	local eid = self:_newentity()
	local typenames = context[self].typenames
	for k,v in pairs(obj) do
		local tc = typenames[k]
		if not tc then
			error ("Invalid key : ".. k)
		end
		local id = self:_addcomponent(eid, tc.id)
		self:object(k, id, v)
	end
end

local ref_key = setmetatable({} , { __index = function(cache, key)
	local select_key = string.format("%s_dead:out %s_live?out %s:new", key, key, key)
	cache[key] = select_key
	return select_key
end })

function M:ref(name, obj)
	local ctx = context[self]
	local typenames = ctx.typenames
	local tc = assert(typenames[name])
	local live = name .. "_live"
	local dead = name .. "_dead"
	obj = obj or tc.tag
	for v in self:select(dead) do
		v[dead] = false
		v[live] = true
		v[name] = obj
		return self:sync(ref_key[name] , v)
	end
	local eid = self:_newentity()
	local id = self:_addcomponent(eid, tc.id)
	self:object(name, id, obj)
	self:object(live, self:_addcomponent(eid, typenames[live].id), true)
	return id
end

function M:release(name, refid)
	local id = assert(context[self].typenames[name].id)
	self:_release(id, refid)
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

function M:sync(pat, iter)
	local p = context[self].select[pat]
	return self:_sync(p, iter)
end

function M:clear(name)
	local id = assert(context[self].typenames[name].id)
	self:_clear(id)
end

function M:sort(sorted, name)
	local ctx = context[self]
	local typenames = ctx.typenames
	local t = assert(typenames[name])
	assert(t.type == typeid.int or (#t == 1 and t[1][1] == typeid.int))
	local stype = typenames[sorted]
	if stype == nil then
		local id = ctx.id + 1
		assert(id <= ecs._MAXTYPE)
		ctx.id = id
		stype = {
			id = id,
			name = sorted,
			size = ecs._ORDERKEY,
			tag = true
		}
		self:_newtype(id, stype.size)
		typenames[sorted] = stype
	else
		assert(stype.size == ecs._ORDERKEY)
	end
	self:_sortkey(stype.id, t.id)
end

function M:bsearch(sorted, name, value)
	local typenames = context[self].typenames
	local sorted_id = typenames[sorted].id
	local value_id = typenames[name].id
	return self:_bsearch(sorted_id, value_id, value)
end

do
	local _object = M._object
	function M:object(name, refid, v)
		local pat = context[self].ref[name]
		return _object(pat, v, refid)
	end

	function M:singleton(name, v)
		local pat = context[self].ref[name]
		return _object(pat, v)
	end
end

function ecs.world()
	local w = ecs._world()
	context[w].typenames.REMOVED = {
		name = "REMOVED",
		id = ecs._REMOVED,
		size = 0,
		tag = true,
	}
	return w
end

return ecs
