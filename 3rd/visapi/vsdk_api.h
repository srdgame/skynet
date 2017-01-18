#ifndef __vsdk_api_H__
#define __vsdk_api_H__
// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the VISTEK_SYSTEM_SDK_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// VISTEK_SYSTEM_SDK_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#include "vsdk_defs.h"

#ifdef __cplusplus
extern "C" {
#endif
	VSDK_API int __stdcall VSDK_Init();

	VSDK_API int __stdcall VSDK_Cleanup();

	//************************************
	// Method:    VSDK_Login
	// FullName:  VSDK_Login
	// Access:    public 
	// Returns:   int errorcode
	// Qualifier:
	// Parameter: const char * ip: hostname or ip addr
	// Parameter: int port: service port
	// Parameter: const char * username
	// Parameter: const char * password
	// Parameter: fp_data_callback fpCallback;
	// Parameter:  void* pUserData; user pointer for callback
	// Output Parameter: handle of login
	//************************************
	VSDK_API int __stdcall VSDK_Login(const char* ip, int port, const char* un, const char* pw, fp_data_callback fpCallback, void* pUserData, void** pHDL);

	//************************************
	// Method:    VSDK_Logout
	// FullName:  VSDK_Logout
	// Access:    public 
	// Returns:   int errorcode
	// Qualifier:
	// Parameter: void * hdl
	//************************************
	VSDK_API int __stdcall VSDK_Logout(void* hdl);

	//************************************
	// Method:    VSDK_CheckConnection
	// FullName:  VSDK_CheckConnection
	// Access:    public 
	// Returns:   int errorcode
	// Qualifier:
	// Parameter: void * hdl
	//************************************
	VSDK_API int __stdcall VSDK_CheckConnection(void* hdl);

	//************************************
	// Method:    VSDK_Request
	// FullName:  VSDK_Request
	// Access:    public 
	// Returns:	  int errorcode
	// Qualifier:
	// Parameter: void * hdl
	// Parameter: int request_type
	// Parameter: const char * uri; e.g: vistek://$(host):$(port)/resource/$(resource_name)
	// $(resource_name): refresh_all, device_list, group_list, device_status_list
	// Parameter: const char * content
	// Parameter: char * out_buff
	// Parameter: int out_buff_size
	// Parameter: int * actual_size
	//************************************
	VSDK_API int __stdcall VSDK_Request(void* hdl, int request_type, const char* uri, const char* content, char* out_buff, int out_buff_size, int* actual_size);

	//************************************
	// Method:    VSDK_GetLastData
	// FullName:  VSDK_GetLastData
	// Access:    public 
	// Returns:	  int errorcode
	// Qualifier:
	// Parameter: void * hdl
	// Parameter: char * out_buff
	// Parameter: int out_buff_size
	// Parameter: int * actual_size
	//************************************
	VSDK_API int __stdcall VSDK_GetLastData(void* hdl, char* out_buff, int out_buff_size, int* actual_size);

	/************************************
	// Method:    VSDK_Subscribe
	// FullName:  VSDK_Subscribe
	// Access:    public 
	// Returns:   VISTEK_DATA_API int __stdcall
	// Qualifier:
	// Parameter: void * hdl
	// Parameter: const char * uri; e.g: vistek://$(host):$(port)/subscribe/$(resource_name)
	// $(resource_name): device, device_status, group
	// Parameter: const char * content
	// Sample:
	
	<event subject="connection" ec="6001" desc="bad connection"/>

	<data_change type="add">
	<device/>
	</data_change>

	<data_change type="modify">
	<group/>
	</data_change>

	<data_change type="delete">
	<group/>
	</data_change>
	//************************************
	*/
	VSDK_API int __stdcall VSDK_Subscribe(void* hdl, const char* uri, const char* content);


	VSDK_API int __stdcall VSDK_Unsubscribe(void* hdl, const char* uri);

	VSDK_API long long __stdcall VSDK_GetConsoleHWND();

#ifdef __cplusplus
}
#endif
#endif