#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32 // MinGW
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#else
#ifdef MINGW32_BUILD
#include <sys/time.h>
#include <sys/types.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#endif
// TODO: for win32 native
#endif

#include <lua.h>
#include <lauxlib.h>
#include <vistek_error_code.h>
#include "lua_visapi.h"
#include "threadpool.h"

#ifndef VISAPI_MODNAME
#define VISAPI_MODNAME "visapi"
#endif

#ifndef VISAPI_VERSION
#define VISAPI_VERSION "0.2.0"
#endif

#define VISAPI_CONN "VISAPI_CONN_KEY"
#define DEFAULT_BUF_SIZE 1024 * 1024 * 8


static void ERROR_ABORT(x, s) {
	fprintf(stderr, "%d : %s\n", x, s);
	exit(-1);
}

DLL_VSDK_T g_DLL;
void* g_DLL_Handle = NULL;

typedef struct {
	lua_State* state;
	int callback;
} conn_handle_t;

typedef struct {
	int closed;
	int env;
	void* handle;
	conn_handle_t* data;
} conn_data_t;

typedef struct callback_data {
	void* handle;
	vsdk_callback_data_header_t header;
	char* data;
	int size;
	conn_handle_t* cb_data;
	void* reserved0;
} callback_data_t;

typedef struct callback_list_node {
	callback_data_t data;
	struct callback_list_node *next;
} callback_list_node_t;

callback_list_node_t *cb_tail = NULL;
callback_list_node_t *cb_head = NULL;

pthread_mutex_t callback_mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t callback_cond = PTHREAD_COND_INITIALIZER;
//
// For async request callback
typedef struct request_async_data {
	conn_handle_t cb_data;
	conn_data_t* conn;
	int request_type;
	char* uri;
	char* content;
	char* response;
	int response_size;
	int ret_code;
} request_async_data_t;

typedef struct response_list_node {
	request_async_data_t* data;
	struct response_list_node *next;
} response_list_node_t;

threadpool_t* req_pool = NULL;
response_list_node_t *cb_resp_tail = NULL;
response_list_node_t *cb_resp_head = NULL;
pthread_mutex_t resp_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static int visapi_failmsg(lua_State *L, const char *err, const char *m) {
	lua_pushnil(L);
	lua_pushliteral(L, "VISTEK API: ");
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

static int traceback (lua_State *L) {
	if (!lua_isstring(L, 1))  /* 'message' not a string? */
		return 1;  /* keep it intact */
	lua_pushglobaltable(L);
	lua_getfield(L, -1, "debug");
	lua_remove(L, -2);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	lua_pushvalue(L, 1);  /* pass error message */
	lua_pushinteger(L, 2);  /* skip this function and traceback */
	lua_call(L, 2, 1);  /* call debug.traceback */
	return 1;
}


static void visapi_check_callback(lua_State* L, conn_handle_t* data, int index)
{
	luaL_checktype(L, index, LUA_TFUNCTION);
	luaL_unref(L, LUA_REGISTRYINDEX, data->callback);
	lua_pushvalue(L, index);
	data->callback = luaL_ref(L, LUA_REGISTRYINDEX);
}

static void visapi_call_callback(lua_State* L, conn_handle_t* data, int nargs) {
	int ref = data->callback;
	if (ref == LUA_NOREF) {
		lua_pop(L, nargs);
	} else {
		// Get the traceback function in case of error
		lua_pushcfunction(L, traceback);
		// And insert it before the args if there are any.
		if (nargs) {
			lua_insert(L, -1 - nargs);
		}
		// Get the callback
		lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
		// And insert it before the args if there are any.
		if (nargs) {
			lua_insert(L, -1 - nargs);
		}

		if (lua_pcall(L, nargs, 0, -2 - nargs)) {
			fprintf(stderr, "Uncaught Error: %s\n", lua_tostring(L, -1));
			exit(-1);
		}
		// Remove the traceback function
		lua_pop(L, 1);
	}
}


/*
** Check for valid connection.
*/
static conn_data_t *get_connection (lua_State *L) {
	conn_data_t *conn = (conn_data_t *)luaL_checkudata (L, 1, VISAPI_CONN);
	luaL_argcheck (L, conn != NULL, 1, "connection expected");
	luaL_argcheck (L, !conn->closed, 1, "connection is closed");
	return conn;
}

#define LOAD_DLL_FUNC(FUNC) \
	do { \
		*(void **)&g_DLL.FUNC = dlsym(g_DLL_Handle, #FUNC);\
		err = dlerror(); \
		if (err != NULL) { \
			dlclose(g_DLL_Handle); \
			fprintf(stderr, "%s\n", err);\
			exit(EXIT_FAILURE); \
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


int load_vsdk(const char* dllname)
{
	if (g_DLL.Loaded != 0) {
		g_DLL.VSDK_Cleanup();
		dlclose(g_DLL_Handle);
		memset(&g_DLL, 0, sizeof(DLL_VSDK_T));
		g_DLL_Handle = 0;
	}

	char* err;
	if (NULL == dllname || strlen(dllname) == 0) {
#ifndef WIN32
		dllname = "libvistek_system_sdk.so";
#else
		dllname = "vistek_system_sdk-1.dll";
#endif
	}
	printf("loading dll %s\n", dllname);
	g_DLL_Handle = dlopen(dllname, RTLD_LAZY);
	if (!g_DLL_Handle) {
		fprintf(stderr, "%s\n", dlerror());
		exit(EXIT_FAILURE);
	}

	dlerror();

	LOAD_DLL_FUNC(VSDK_Init);
	LOAD_DLL_FUNC(VSDK_Cleanup);
	LOAD_DLL_FUNC(VSDK_Login);
	LOAD_DLL_FUNC(VSDK_Logout);
	LOAD_DLL_FUNC(VSDK_CheckConnection);
	LOAD_DLL_FUNC(VSDK_Request);
	LOAD_DLL_FUNC(VSDK_GetLastData);
	LOAD_DLL_FUNC(VSDK_Subscribe);
	LOAD_DLL_FUNC(VSDK_Unsubscribe);
	LOAD_DLL_FUNC(VSDK_GetConsoleHWND);
	LOAD_DLL_OPT_FUNC(VSDK_Set_PrintLogLevel);
	LOAD_DLL_OPT_FUNC(VSDK_Get_PrintLogLevel);
	g_DLL.Loaded = 1;
	g_DLL.Version = 1;
	return 0;
}

static int visapi_request(lua_State *L)
{
	conn_data_t* conn = get_connection(L);
	int request_type = luaL_checkinteger(L, 2);
	const char* uri = luaL_checkstring(L, 3);
	const char* content = luaL_optstring(L, 4, NULL);
	int expact_size = DEFAULT_BUF_SIZE;
	int ec = 0;
	int act_size = 0;
	int rc = 2;
	char* buf = malloc(expact_size * sizeof(char));
	memset(buf, 0, expact_size);
	if (buf == NULL) {
		return visapi_failmsg(L, "Memory Issue: ", "Cannot alloc memory");
	}
	ec = g_DLL.VSDK_Request(conn->handle, request_type, uri, content, buf, expact_size, &act_size);
	printf("[VISAPI] Request Result: %d, act_size: %d\n", ec, act_size);
	if (ec == v_ERR_Buffer_Full) {
		expact_size = act_size + 1024;
		buf = realloc(buf, expact_size);
		act_size = 0;
		ec = g_DLL.VSDK_Request(conn->handle, request_type, uri, content, buf, expact_size, &act_size);
	}
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "VSDK_Request failure: code[%d]", ec);
		rc = visapi_failmsg(L, err_msg, buf);
	} else {
		lua_pushboolean (L, 1);
		lua_pushlstring(L, buf, act_size);
	}
	free(buf);
	return rc;
}

static void call_request_async(void* arg)
{
	request_async_data_t* data = (request_async_data_t*)arg;

	int expact_size = DEFAULT_BUF_SIZE;
	int ec = 0;
	int act_size = 0;
	int rc = 1;
	char* buf = malloc(expact_size * sizeof(char));
	memset(buf, 0, expact_size);
	if (buf == NULL) {
		ERROR_ABORT(-1, "Memory Issue: Cannot alloc memory");
	}
	ec = g_DLL.VSDK_Request(data->conn->handle, data->request_type, data->uri, data->content, buf, expact_size, &act_size);
	printf("[VISAPI] Request Result: %d, act_size: %d\n", ec, act_size);
	if (ec == v_ERR_Buffer_Full) {
		expact_size = act_size + 1024;
		buf = realloc(buf, expact_size);
		act_size = 0;
		ec = g_DLL.VSDK_Request(data->conn->handle, data->request_type, data->uri, data->content, buf, expact_size, &act_size);
	}

	data->ret_code = ec;
	data->response = buf;
	data->response_size = act_size;

	int status = pthread_mutex_lock(&resp_list_mutex);
	if(status != 0)
		ERROR_ABORT(status, "pthread mutex locking..");

	/* Insert to list */
	response_list_node_t* node = (response_list_node_t*)malloc(sizeof(response_list_node_t));
	node->data = data;
	node->next = NULL;
	
	if (!cb_resp_head) {
		cb_resp_head = node;
		cb_resp_tail = node;
	} else {
		cb_resp_tail->next = node;
		cb_resp_tail = node;
	}

	status = pthread_mutex_unlock(&resp_list_mutex);
	if(status != 0)
		ERROR_ABORT(status, "pthread mutex unlocking...");
}

static int visapi_request_async(lua_State *L)
{
	request_async_data_t* data = (request_async_data_t*)malloc(sizeof(request_async_data_t));
	data->cb_data.state = L;
	data->cb_data.callback = LUA_NOREF;
	data->conn = get_connection(L);
	data->request_type = luaL_checkinteger(L, 2);
	data->uri = strdup(luaL_checkstring(L, 3));
	data->content = strdup(luaL_optstring(L, 4, NULL));
	visapi_check_callback(L, &data->cb_data, 5);

	if (0 != threadpool_add(req_pool, &call_request_async, data, 0)) {
		free(data->uri);
		free(data->content);
		free(data);
		return visapi_failmsg(L, "VSDK_Request_Async failure: ", "ThreadPool Full!!");
	}

	lua_pushboolean (L, 1);
	return 1;
}

static int visapi_get_last_data(lua_State *L)
{
	conn_data_t* conn = get_connection(L);
	int expact_size = DEFAULT_BUF_SIZE;
	int ec = 0;
	int act_size = 0;
	int rc = 1;
	char* buf = malloc(expact_size * sizeof(char));
	if (buf == NULL) {
		return visapi_failmsg(L, "Memory Issue: ", "Cannot alloc memory");
	}
	ec = g_DLL.VSDK_GetLastData(conn->handle, buf, expact_size, &act_size);
	if (ec == v_ERR_Buffer_Full) {
		expact_size = act_size + 1024;
		buf = realloc(buf, expact_size);
		act_size = 0;
		ec = g_DLL.VSDK_GetLastData(conn->handle, buf, expact_size, &act_size);
	}
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		rc = rc + visapi_failmsg(L, "VSDK_GetLastData failure: ", err_msg);
	}
	lua_pushlstring(L, buf, act_size);
	free(buf);
	return rc;
}

static int visapi_subscribe(lua_State *L)
{
	conn_data_t* conn = get_connection(L);
	const char* uri = luaL_checkstring(L, 2);
	const char* condition = luaL_optstring(L, 3, "");
	int ec = g_DLL.VSDK_Subscribe(conn->handle, uri, condition);
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "VSDK_Subscribe failure: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;

}

static int visapi_unsubscribe(lua_State *L)
{
	conn_data_t* conn = get_connection(L);
	const char* uri = luaL_checkstring(L, 2);

	int ec = g_DLL.VSDK_Unsubscribe(conn->handle, uri);
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "VSDK_Unsubscribe failure: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static int visapi_gc(lua_State *L)
{
	conn_data_t *conn=(conn_data_t *)luaL_checkudata(L, 1, VISAPI_CONN);
	if (conn != NULL && !(conn->closed)) {
		/* Nullify structure fields. */
		conn->closed = 1;
		luaL_unref (L, LUA_REGISTRYINDEX, conn->env);
		conn->env = LUA_NOREF;

		g_DLL.VSDK_Logout(conn->handle);

		if (conn->data) {
			luaL_unref (L, LUA_REGISTRYINDEX, conn->data->callback);
			free(conn->data);
			conn->data = NULL;
		}
	}
	return 0;
}

static int visapi_close (lua_State *L) {
	conn_data_t *conn = (conn_data_t *)luaL_checkudata (L, 1, VISAPI_CONN);
	luaL_argcheck (L, conn != NULL, 1, "VISTECK API: connection expected");
	if (conn->closed) {
		lua_pushboolean (L, 0);
		return 1;
	}
	visapi_gc(L);
	lua_pushboolean (L, 1);
	return 1;
}

static int visapi_check_connection(lua_State *L) {
	conn_data_t* conn = get_connection(L);

	int ec = g_DLL.VSDK_CheckConnection(conn->handle);
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "VSDK_CheckConnection failure: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static const struct luaL_Reg api_funcs[] = {
	{ "__gc", visapi_gc },
	{ "close", visapi_close },
	{ "request", visapi_request },
	{ "request_async", visapi_request_async },
	{ "subscribe", visapi_subscribe },
	{ "unsubscribe", visapi_unsubscribe },
	{ "check_connection", visapi_check_connection },
	{ "get_last_data", visapi_get_last_data },
	{ NULL, NULL},
};

static void __stdcall callback_handler(void* handle, vsdk_callback_data_header_t* header, const char* data, int size, void* pUserData, void* reserved0)
{
	printf("[VISAPI CALLBACK] header.src_type %d\t header.data_type%d \n", header->src_type, header->data_type);
	printf("[VISAPI CALLBACK] got: %d bytes; userdata 0x%lX \n", size, (long)pUserData);

	int status = pthread_mutex_lock(&callback_mutex);
	if(status != 0)
		ERROR_ABORT(status, "pthread mutex locking..");

	/* Insert to list */
	callback_list_node_t* node = (callback_list_node_t*)malloc(sizeof(callback_list_node_t));
	node->data.handle = handle;
	node->data.data = (char*)malloc(sizeof(char) * size);
	node->data.size = size;
	node->data.cb_data = (conn_handle_t*)pUserData;
	node->data.reserved0 = reserved0;
	node->next = NULL;
	memcpy(&node->data.header, header, sizeof(vsdk_callback_data_header_t));
	memcpy(node->data.data, data, size);
	
	if (!cb_head) {
		cb_head = node;
		cb_tail = node;
	} else {
		cb_tail->next = node;
		cb_tail = node;
	}

	/*
	status = pthread_cond_signal(&callback_cond);
	if(status != 0)
		ERROR_ABORT(status,"Signal cond");
	*/

	status = pthread_mutex_unlock(&callback_mutex);
	if(status != 0)
		ERROR_ABORT(status, "pthread mutex unlocking...");
}

/*
static int parse_request_info(lua_State *L, int opt, login_request_info_t *info)
{
	if (lua_type(L, opt) != LUA_TTABLE) {
		printf("%s\n", "We need a table for requset info");
		return -1;
	}
	lua_getfield(L, opt, "type");
	if (!lua_isnil(L, -1)) {
		info->_type = (credential_type_t)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
	}

	if (info->_type == ct_PASSWORD) {
		lua_getfield(L, opt, "username");
		if (!lua_isnil(L, -1)) {
			info->_credential._un_pw._un = luaL_checkstring(L, -1);
			lua_pop(L, 1);
		} else {
			printf("%s\n", "Username not found");
			return -1;
		}
		lua_getfield(L, opt, "password");
		if (!lua_isnil(L, -1)) {
			info->_credential._un_pw._pw = luaL_checkstring(L, -1);
			lua_pop(L, 1);
		} else {
			printf("%s\n", "Username not found");
			return -1;
		}
	}

	if (info->_type == ct_SYSTEM_TOKEN) {
		lua_getfield(L, opt, "token");
		if (!lua_isnil(L, -1)) {
			info->_credential._token = luaL_checkstring(L, -1);
			lua_pop(L, 1);
		} else {
			printf("%s\n", "Token not found");
			return -1;
		}
	}
	if (info->_type == ct_PUBLIC_KEY) {
	}
	return 0;
}
*/
static int create_connection (lua_State *L, conn_handle_t* data, int env, void * handle) {
	conn_data_t *conn = (conn_data_t *)lua_newuserdata(L, sizeof(conn_data_t));
	luaL_getmetatable (L, VISAPI_CONN);
	lua_setmetatable (L, -2);

	/* fill in structure */
	conn->closed = 0;
	conn->env = LUA_NOREF;
	conn->handle = handle;
	conn->data = data;
	lua_pushvalue(L, env);
	conn->env = luaL_ref(L, LUA_REGISTRYINDEX);

	//callback(handle, "hello", 5, data, NULL);

	return 1;
}

static int env_data_connect(lua_State *L)
{
	int ec = 0;
	void* handle = NULL;
	//login_request_info_t info;

	const char* address = luaL_checkstring(L, 1);
	const int port = luaL_checkinteger(L, 2);
	const char* username = luaL_checkstring(L, 3);
	const char* passwd = luaL_checkstring(L, 4);

	conn_handle_t* data = (conn_handle_t*) malloc(sizeof(conn_handle_t));
	data->state = L;
	data->callback = LUA_NOREF;
/*
	ec = parse_request_info( L, 3, &info );
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "Parse RequestInfo Error: ", err_msg);
	}
	// Connectio
	printf("Credential Type: %d  Username: %s Password: %s\n", info._type, info._credential._un_pw._un, info._credential._un_pw._pw);

	ec = g_DLL.VSDK_Login(address, port, &info, callback, data, &handle);
	*/
	printf("[VISAPI] Address: %s\tPort: %d\tUsername: %s\tPassword:%s\n", address, port, username, passwd);
	ec = g_DLL.VSDK_Login(address, port, username, passwd, (fp_data_callback)callback_handler, data, &handle);
	printf("[VISAPI] Login Result: %d\n", ec);
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "Login Error: ", err_msg);
	}

	visapi_check_callback(L, data, 5);

	return create_connection(L, data, 1, handle);
}

static int env_init(lua_State *L)
{
	req_pool = threadpool_create(16, 32, 0);
	if (!req_pool) {
		char err_msg[100];
		sprintf(err_msg, "Failed init thread pool\n");
		return visapi_failmsg(L, "ThreadPool failure: ", err_msg);
	}

	const char* dllname = luaL_optstring(L, 1, "");
	int ec = load_vsdk(dllname);
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "Load SDK DLL failure: ", err_msg);
	}
	ec = g_DLL.VSDK_Init();
	if (ec != 0) {
		char err_msg[100];
		sprintf(err_msg, "error code[%d]", ec);
		return visapi_failmsg(L, "VSDK_Init failure: ", err_msg);
	}
	lua_pushboolean (L, 1);
	return 1;
}

static int env_set_print_log_level(lua_State *L)
{
	int level = luaL_checkinteger(L, 1);
	if (g_DLL.VSDK_Set_PrintLogLevel) {
		g_DLL.VSDK_Set_PrintLogLevel(level);
	}
	level = 99;
	if (g_DLL.VSDK_Get_PrintLogLevel) {
		level = g_DLL.VSDK_Get_PrintLogLevel();
	}
	lua_pushinteger(L, level);
	return 1;
}

static int env_get_print_log_level(lua_State *L)
{
	int level = 99;
	if (g_DLL.VSDK_Get_PrintLogLevel) {
		level = g_DLL.VSDK_Get_PrintLogLevel();
	}
	lua_pushinteger(L, level);
	return 1;
}

static int env_getpid(lua_State *L)
{
	int pid = getpid();
	lua_pushinteger(L, pid);
	return 1;
}

static void process_node(callback_data_t* data)
{
	if (!data->cb_data)
		return;

	lua_State* L = data->cb_data->state;
	if (!L)
		return;

	lua_newtable(L);
	lua_pushinteger(L, data->header.src_type);
	lua_setfield(L, -2, "src_type");
	lua_pushinteger(L, data->header.data_type);
	lua_setfield(L, -2, "data_type");
	
	switch(data->header.src_type) {
		case vcds_EVENT:
		{
			lua_pushlstring(L, data->header.event_header.event_type, strlen(data->header.event_header.event_type));
			lua_setfield(L, -2, "event_type");
			lua_pushlstring(L, data->header.event_header.event_source, strlen(data->header.event_header.event_source));
			lua_setfield(L, -2, "event_source");
		} break;
		case vcds_SUBSCRIBE:
		{
			lua_pushlstring(L, data->header.src_uri, strlen(data->header.src_uri));
			lua_setfield(L, -2, "src_uri");
		} break;
		case vcds_MEDIA:
		{
			lua_pushlstring(L, data->header.session_id, strlen(data->header.session_id));
			lua_setfield(L, -2, "session_id");
		} break;
		default:
		{
			lua_pushlstring(L, data->header.session_id, strlen(data->header.session_id));
			lua_setfield(L, -2, "header_data");
		} break;
	}
	
	lua_pushlstring(L, data->data, data->size);
	/**
	 * TODO: for data->reserved0
	 */

	visapi_call_callback(L, data->cb_data, 2);
}

static void process_list()
{
	callback_list_node_t* node;
	while (cb_head) {
		node = cb_head;
		process_node(&node->data);
		cb_head = node->next;
		free(node->data.data);
		free(node);
	}
}

static void process_resp(lua_State* L)
{
	response_list_node_t* node;
	while (cb_resp_head) {
		node = cb_resp_head;
		request_async_data_t* data = node->data;

		//lua_State* L = data->cb_data.state;
		if (L) {
			lua_pushinteger(L, data->ret_code);
			lua_pushlstring(L, data->response, data->response_size);
			visapi_call_callback(L, &data->cb_data, 2);
		}

		cb_resp_head = node->next;
		free(data->uri);
		free(data->content);
		free(data->response);
		free(data);
		free(node);
	}
}


static int env_run(lua_State *L)
{
	struct timeval now;
	struct timespec outtime;
	//const int timeout = luaL_optinteger(L, 1, 100);

	int status = pthread_mutex_lock(&callback_mutex);
	if(status != 0)
		ERROR_ABORT(status, "lock");
	process_list();

	/*
	gettimeofday(&now, NULL);
	int nsec = (now.tv_usec * 1000) + ((timeout % 1000) *1000000);
	outtime.tv_sec = now.tv_sec + (timeout / 1000) + nsec / 1000000000;
	outtime.tv_nsec = nsec % 1000000000;

	status = pthread_cond_timedwait(&callback_cond, &callback_mutex, &outtime);
	//if(status != 0 )
	//	ERROR_ABORT(status, "Wait cond");

	//if (status != ETIMEDOUT) {
	if (status == 0) {
		process_list();
	}
	*/

	status = pthread_mutex_unlock(&callback_mutex);
	if(status != 0)
		ERROR_ABORT(status, "Unlock mutex");

	// Process response
	status = pthread_mutex_lock(&resp_list_mutex);
	if(status != 0)
		ERROR_ABORT(status, "lock");
	process_resp(L);
	status = pthread_mutex_unlock(&resp_list_mutex);
	if(status != 0)
		ERROR_ABORT(status, "Unlock mutex");

	lua_pushboolean (L, 1);
	return 1;
}

static int visapi_tostring (lua_State *L) {
	char buff[100];
	conn_data_t *obj = (conn_data_t *)lua_touserdata (L, 1);
	if (obj->closed)
		strcpy (buff, "closed");
	else
		sprintf (buff, "%p", (void *)obj);
	lua_pushfstring (L, "%s (%s)", lua_tostring(L,lua_upvalueindex(1)), buff);
	return 1;
}

static int lua_visapi_init_meta(lua_State *L)
{

	if (!luaL_newmetatable(L, VISAPI_CONN))
		return 0;
	luaL_setfuncs(L, api_funcs, 0);

	/* define metamethods */
	lua_pushliteral (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushliteral (L, "__tostring");
	lua_pushstring (L, VISAPI_CONN);
	lua_pushcclosure (L, visapi_tostring, 1);
	lua_settable (L, -3);

	lua_pushliteral (L, "__metatable");
	lua_pushliteral (L, "VISTEK API: you're not allowed to get this metatable");
	lua_settable (L, -3);

	lua_pop(L, 1);

	return 1;
}

static const struct luaL_Reg env_funcs[] = {
	{ "init", env_init },
	{ "connect", env_data_connect },
	{ "run", env_run },
	{ "set_pll", env_set_print_log_level },
	{ "get_pll", env_get_print_log_level },
	{ "getpid", env_getpid },
	{ NULL, NULL },
};

typedef struct {
	const char *name;
	int value;
} lua_visapi_int_const;

#define DEFINE_RT_CONST(NAME) {#NAME, rt_##NAME}
//#define DEFINE_CT_CONST(NAME) {#NAME, ct_##NAME}
#define DEFINE_INT_CONST(NAME) {#NAME, NAME}
#define DEFINE_VDT_CONST(NAME) {#NAME, vdt_##NAME}
#define DEFINE_VCDS_CONST(NAME) {#NAME, vcds_##NAME}

static void lua_visapi_register_consts(lua_State *L, const lua_visapi_int_const *c){
	const lua_visapi_int_const *v;
	for(v = c; v->name; ++v){
		lua_pushinteger(L, v->value);
		lua_setfield(L, -2, v->name);
	}
}


const lua_visapi_int_const request_types[] ={
	DEFINE_RT_CONST(  GET  ),
	DEFINE_RT_CONST(  INSERT  ),
	DEFINE_RT_CONST(  UPDATE  ),
	DEFINE_RT_CONST(  DELETE  ),

	{NULL, 0}
};

/*
const lua_visapi_int_const credential_types[] ={
	DEFINE_CT_CONST(  PASSWORD  ),
	DEFINE_CT_CONST(  SYSTEM_TOKEN  ),
	DEFINE_CT_CONST(  PUBLIC_KEY  ),

	{NULL, 0}
};
*/

const lua_visapi_int_const data_types[] = {
	DEFINE_VDT_CONST( NONE ),
	DEFINE_VDT_CONST( XML ),
	DEFINE_VDT_CONST( JSON ),
	DEFINE_VDT_CONST( TEXT ),
	DEFINE_VDT_CONST( MEDIASAMPE ),

	{NULL, 0}
};

const lua_visapi_int_const data_source_types[] = {
	DEFINE_VCDS_CONST( EVENT ),
	DEFINE_VCDS_CONST( SUBSCRIBE ),
	DEFINE_VCDS_CONST( MEDIA ),

	{NULL, 0}
};

static int lua_visapi_new(lua_State *L)
{
	lua_visapi_init_meta(L);

	// Create vistek_data module 
	lua_newtable(L);

	// Register env functions
	luaL_setfuncs(L, env_funcs, 0);

	// Consts
	lua_visapi_register_consts(L, request_types);
	//lua_visapi_register_consts(L, credential_types);
	lua_visapi_register_consts(L, data_types);
	lua_visapi_register_consts(L, data_source_types);

	/* Set visapi.null */
	lua_pushlightuserdata(L, NULL);
	lua_setfield(L, -2, "null");

	/* Set module name / version fields */
	lua_pushliteral(L, VISAPI_MODNAME);
	lua_setfield(L, -2, "_NAME");
	lua_pushliteral(L, VISAPI_VERSION);
	lua_setfield(L, -2, "_VERSION");

	return 1;
}

void visapi_init()
{
	if (NULL == g_DLL_Handle)
		memset(&g_DLL, 0, sizeof(DLL_VSDK_T));
	else
		printf("[VISAPI] Do not need to reset the g_DLL\n");
}

void visapi_cleanup()
{
	threadpool_destroy(req_pool, 0);

	printf("[VISAPI] Cleanup.....\n");
	if (g_DLL.Loaded) {
		printf("[VISAPI] SDK Cleanup!!!!!!!!\n");
		int rc = g_DLL.VSDK_Cleanup();
		printf("[VISAPI] SDK Cleanup end %d\n", rc);
		dlclose(g_DLL_Handle);
	}
	printf("[VISAPI] Cleanup end \n");
}

int luaopen_visapi(lua_State *L) {
	visapi_init();
	//atexit(visapi_cleanup);

	int ret = lua_visapi_new(L);

	return ret;
}
