#LUA_INC=-Id:/projects/lua-5.4.2/src
#LUA_LIB=-Ld:/projects/lua-5.4.2/src -llua54

LUA_INC=-I /e/opensource/lua/src
LUA_LIB=-L /usr/local/bin -llua54

CFLAGS=-O2 -Wall
SHARED=--shared -fPIC

all : ecs.dll

ecs.dll : luaecs.c ecs_group.c ecs_persistence.c ecs_template.c ecs_capi.c ecs_entityid.c ecs_cache.c
	gcc $(CFLAGS) $(SHARED) -DTEST_LUAECS -o $@ $^ $(LUA_INC) $(LUA_LIB)

clean :
	rm -f ecs.dll

