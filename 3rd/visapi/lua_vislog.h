#ifndef __API_DEFS_H__
#define __API_DEFS_H__
#include "vsdk_defs.h"

#ifdef __cplusplus
extern "C" {
#endif
	typedef enum vistek_log_level
	{
		v_LOG_NONE = 0,
		v_LOG_FATAL,
		v_LOG_ERR,
		v_LOG_WRN,
		v_LOG_INFO,
		v_LOG_VERBOSE,
		v_LOG_VERBOSE1,
		v_LOG_VERBOSE2,
		v_LOG_VERBOSE3,
		v_LOG_ALL
	} vistek_log_level_t;

	typedef enum vistek_log_flags
	{
		vlf_NONE = 0,
		vlf_WRITE = 1,
		vlf_HEAD = 2,
		vlf_TRACE = 4,
		vlf_APPENDENDLINE = 8,
	} vistek_log_flags_t;

	typedef enum lua_log_level
	{
		LLVL_EMERG     = 1,
		LLVL_ALERT     = 2,
		LLVL_FATAL     = 3,
		LLVL_ERROR     = 4,
		LLVL_WARNING   = 5,
		LLVL_NOTICE    = 6,
		LLVL_INFO      = 7,
		LLVL_DEBUG     = 8,
		LLVL_TRACE     = 9,
		LLVL_COUNT
	} lua_log_level_t;

	typedef int  __stdcall (*DLL_VLOG_Open)(const char* host, int port, const char* id, const char* name, const char* ext);
	typedef void __stdcall (*DLL_VLOG_Close)();
	typedef int  __stdcall (*DLL_VLOG_Send)(const char* category, int level, int flag, const char* file, int line, const char* function, const char* content, const char* ext, long long ts);// 1970.1.1 utc micro seconds

	typedef struct DLL_VLOG {
		int Loaded;
		int Version;
		DLL_VLOG_Open VLOG_Open;
		DLL_VLOG_Close VLOG_Close;
		DLL_VLOG_Send VLOG_Send;
	} DLL_VLOG_T;

#ifdef __cplusplus
}
#endif
#endif
