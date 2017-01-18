#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32 // MinGW
#include <sys/time.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <unistd.h>
#else
#ifdef MINGW32_BUILD
#include <sys/time.h>
#include <sys/types.h>
#define HAVE_STRUCT_TIMESPEC
#include <dlfcn.h>
#include <unistd.h>
#endif
// TODO: for win32 native
#endif

#include <lua.h>
#include <lauxlib.h>
#include <vistek_error_code.h>
#include "lua_vislog.h"

#ifndef VISLOG_MODNAME
#define VISLOG_MODNAME "vislog"
#endif

#ifndef VISLOG_VERSION
#define VISLOG_VERSION "0.1.0"
#endif

#define VISLOG_CONN "VISLOG_CONN_KEY"

static void ERROR_ABORT(x, s) {
	fprintf(stderr, "%d : %s\n", x, s);
	exit(-1);
}

DLL_VLOG_T g_DLL;
void* g_DLL_Handle = NULL;

static int vislog_failmsg(lua_State *L, const char *err, const char *m) {
	lua_pushnil(L);
	lua_pushliteral(L, "VISLOG: ");
	lua_pushstring(L, err);
	lua_pushstring(L, m);
	lua_concat(L, 3);
	return 2;
}

#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM==501
# define lua_pushglobaltable(L) lua_pushvalue(L, LUA_GLOBALSINDEX)
/*
** Adapted from Lua 5.2.0
*/
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
	luaL_checkstack(L, nup, "too many upvalues");
	for (; l->name != NULL; l++) {	/* fill the table with given functions */
		int i;
		for (i = 0; i < nup; i++)	/* copy upvalues to the top */
			lua_pushvalue(L, -nup);
		lua_pushstring(L, l->name);
		lua_pushcclosure(L, l->func, nup);	/* closure with those upvalues */
		lua_settable(L, -(nup + 3));
	}
	lua_pop(L, nup);	/* remove upvalues */
}
#endif

#define LOAD_DLL_FUNC(FUNC) \
	do { \
		*(void **)&g_DLL.FUNC = dlsym(g_DLL_Handle, #FUNC);\
		err = dlerror(); \
		if (err != NULL) { \
			dlclose(g_DLL_Handle); \
			return err; \
		}\
	} while (0)

#define LOAD_DLL_OPT_FUNC(FUNC) \
	do { \
		*(void **)&g_DLL.FUNC = dlsym(g_DLL_Handle, #FUNC);\
		err = dlerror(); \
		if (err != NULL) { \
			fprintf(stderr, "%s\n", err);\
		}\
	} while (0)


const char* load_dll(const char* dllname)
{
	char* err;
	if (g_DLL.Loaded != 0) {
		g_DLL.VLOG_Close();
		dlclose(g_DLL_Handle);
		memset(&g_DLL, 0, sizeof(DLL_VLOG_T));
		g_DLL_Handle = 0;
	}
	memset(&g_DLL, 0, sizeof(DLL_VLOG_T));

	if (NULL != dllname || strlen(dllname) == 0) {
#ifndef WIN32
		dllname = "libvistek_logger-1.so";
#else
		dllname = "vistek_logger-1.dll";
#endif
	}
	g_DLL_Handle = dlopen(dllname, RTLD_LAZY);
	if (!g_DLL_Handle) {
		return dlerror();
	}

	dlerror();

	LOAD_DLL_FUNC(VLOG_Open);
	LOAD_DLL_FUNC(VLOG_Close);
	LOAD_DLL_FUNC(VLOG_Send);
	//LOAD_DLL_OPT_FUNC(VLOG_Get_PrintLogLevel);
	g_DLL.Loaded = 1;
	g_DLL.Version = 1;
	return NULL;
}

typedef struct {
	const char *name;
	int value;
} lua_vislog_int_const;

#define DEFINE_LVL_CONST(NAME) {#NAME, LLVL_##NAME}
//#define DEFINE_CT_CONST(NAME) {#NAME, ct_##NAME}

static void lua_vislog_register_consts(lua_State *L, const lua_vislog_int_const *c){
	const lua_vislog_int_const *v;
	for(v = c; v->name; ++v){
		lua_pushinteger(L, v->value);
		lua_setfield(L, -2, v->name);
	}
}

const int level_mapping[] = {
	v_LOG_NONE,
	v_LOG_FATAL,
	v_LOG_FATAL,
	v_LOG_FATAL,
	v_LOG_ERR,
	v_LOG_WRN,
	v_LOG_INFO,
	v_LOG_INFO,
	v_LOG_VERBOSE,
	v_LOG_VERBOSE1,
};

const lua_vislog_int_const level_types[] ={
	DEFINE_LVL_CONST(  EMERG   ),
	DEFINE_LVL_CONST(  ALERT   ),
	DEFINE_LVL_CONST(  FATAL   ),
	DEFINE_LVL_CONST(  ERROR   ),
	DEFINE_LVL_CONST(  WARNING ),
	DEFINE_LVL_CONST(  NOTICE  ),
	DEFINE_LVL_CONST(  INFO    ),
	DEFINE_LVL_CONST(  DEBUG   ),
	DEFINE_LVL_CONST(  TRACE   ),

	{NULL, 0}
};

static int env_init(lua_State *L)
{
	const char* dllname = luaL_optstring(L, 1, "");
	const char* err = load_dll(dllname);
	if (err != NULL) {
		char err_msg[1024];
		sprintf(err_msg, "Loading error :%s", err);
		return vislog_failmsg(L, "Load LOGGER DLL failure: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static int env_open(lua_State *L)
{
	int ec = 0;

	const char* host = luaL_checkstring(L, 1);
	const int port = luaL_checkinteger(L, 2);
	const char* id = luaL_checkstring(L, 3);
	const char* name = luaL_checkstring(L, 4);
	const char* ext = luaL_optstring(L, 5, "");

	if (0 == g_DLL.Loaded)
	{
		return vislog_failmsg(L, "Errror:", "Logger is not initialized");
	}

	printf("[VISLOG] Host: %s\tPort: %d\tID: %s\tname:%s\text:%s\n", host, port, id, name, ext);
	ec = g_DLL.VLOG_Open(host, port, id, name, ext);
	printf("[VISLOG] Open Result: %d\n", ec);
	if (ec != 0) {
		char err_msg[1024];
		sprintf(err_msg, "error code[%d]", ec);
		return vislog_failmsg(L, "VLOG_Open Error: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static int env_close(lua_State *L)
{
	if (0 != g_DLL.Loaded)
	{
		g_DLL.VLOG_Close();
	}
	lua_pushboolean (L, 1);
	return 1;
}

static int env_log(lua_State *L)
{
	int ec = 0;
	int flags = vlf_WRITE | vlf_APPENDENDLINE | vlf_HEAD;

	const char* category = luaL_checkstring(L, 1);
	const char* content = luaL_checkstring(L, 2);
	int level = luaL_checkinteger(L, 3);
	const int nsec = luaL_optinteger(L, 4, 0);
	const int usec = luaL_optinteger(L, 5, 0);

	long long ts = nsec;
	if (ts != 0)
	{
		ts = ts * 1000000 + (usec / 1000000);
	}

	if (0 == g_DLL.Loaded)
	{
		return vislog_failmsg(L, "Error:", "Logger is not initialized");
	}
	if (level >= LLVL_COUNT)
		level = LLVL_COUNT - 1;

	ec = g_DLL.VLOG_Send(category, level_mapping[level], flags, "", 0, "", content, "", ts);
	if (ec != 0) {
		char err_msg[1024];
		sprintf(err_msg, "error code[%d]", ec);
		return vislog_failmsg(L, "VLOG_Send Error: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static const struct luaL_Reg env_funcs[] = {
	{ "init", env_init },
	{ "open", env_open },
	{ "log", env_log },
	{ "close", env_close },
	{ NULL, NULL },
};

static int lua_vislog_new(lua_State *L)
{
	// Create vistek_data module 
	lua_newtable(L);

	// Register env functions
	luaL_setfuncs(L, env_funcs, 0);

	// Consts
	lua_vislog_register_consts(L, level_types);

	/* Set vislog.null */
	lua_pushlightuserdata(L, NULL);
	lua_setfield(L, -2, "null");

	/* Set module name / version fields */
	lua_pushliteral(L, VISLOG_MODNAME);
	lua_setfield(L, -2, "_NAME");
	lua_pushliteral(L, VISLOG_VERSION);
	lua_setfield(L, -2, "_VERSION");

	return 1;
}

void vislog_cleanup()
{
	printf("[VISLOG] Cleanup.....\n");
	if (g_DLL.Loaded != 0) {
		printf("[VISLOG] SDK Cleanup!!!!!!!!\n");
		g_DLL.VLOG_Close();
		printf("[VISLOG] SDK Cleanup end \n");
		dlclose(g_DLL_Handle);
	}
	printf("[VISLOG] Cleanup end \n");
}

int luaopen_vislog(lua_State *L) {
	memset(&g_DLL, 0, sizeof(DLL_VLOG_T));
	//atexit(vislog_cleanup);

	int ret = lua_vislog_new(L);

	return ret;
}
