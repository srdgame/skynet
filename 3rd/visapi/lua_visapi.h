#ifndef __API_DEFS_H__
#define __API_DEFS_H__
#include "vsdk_defs.h"

#ifdef __cplusplus
extern "C" {
#endif
	typedef int __stdcall (*DLL_VSDK_Init)();
	typedef int __stdcall (*DLL_VSDK_Cleanup)();

	typedef int __stdcall (*DLL_VSDK_Login)(const char* ip, int port, const char* un, const char* pw, fp_data_callback fpCallback, void* pUserData, void** pHDL);
	typedef int __stdcall (*DLL_VSDK_Logout)(void* hdl);

	typedef int __stdcall (*DLL_VSDK_CheckConnection)(void* hdl);
	typedef int __stdcall (*DLL_VSDK_Request)(void* hdl, int request_type, const char* uri, const char* content, char* out_buff, int out_buff_size, int* actual_size);
	typedef int __stdcall (*DLL_VSDK_GetLastData)(void* hdl, char* out_buff, int out_buff_size, int* actual_size);
	typedef int __stdcall (*DLL_VSDK_Subscribe)(void* hdl, const char* uri, const char* content);
	typedef int __stdcall (*DLL_VSDK_Unsubscribe)(void* hdl, const char* uri);

	typedef long long __stdcall (*DLL_VSDK_GetConsoleHWND)();

	typedef void __stdcall (*DLL_VSDK_Set_PrintLogLevel)(int level);
	typedef int __stdcall (*DLL_VSDK_Get_PrintLogLevel)();


	typedef struct DLL_VSDK {
		int Loaded;
		int Version;
		DLL_VSDK_Init VSDK_Init;
		DLL_VSDK_Cleanup VSDK_Cleanup;
		DLL_VSDK_Login VSDK_Login;
		DLL_VSDK_Logout VSDK_Logout;
		DLL_VSDK_CheckConnection VSDK_CheckConnection;
		DLL_VSDK_Request VSDK_Request;
		DLL_VSDK_GetLastData VSDK_GetLastData;
		DLL_VSDK_Subscribe VSDK_Subscribe;
		DLL_VSDK_Unsubscribe VSDK_Unsubscribe;
		DLL_VSDK_GetConsoleHWND VSDK_GetConsoleHWND;
		DLL_VSDK_Set_PrintLogLevel VSDK_Set_PrintLogLevel;
		DLL_VSDK_Get_PrintLogLevel VSDK_Get_PrintLogLevel;
	} DLL_VSDK_T;

#ifdef __cplusplus
}
#endif
#endif
