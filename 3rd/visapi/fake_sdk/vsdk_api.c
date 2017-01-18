#include "vsdk_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int VSDK_Init() {
	return 0;
}

int VSDK_Cleanup() {
	return 0;
}

int VSDK_Login(const char* ip, int port, const char* un, const char* pw, fp_data_callback fpCallback, void* pUserData, void** pHDL){
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

int VSDK_Logout(void* hdl) {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

int VSDK_CheckConnection(void* hdl) {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

int VSDK_Request(void* hdl, int request_type, const char* uri, const char* content, char* out_buff, int out_buff_size, int* actual_size) {
	sprintf(out_buff, "%s", "Test SDK Result");
	*actual_size = strlen("Test SDK Result");
	return 0;
}

int VSDK_GetLastData(void* hdl, char* out_buff, int out_buff_size, int* actual_size) {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

int VSDK_Subscribe(void* hdl, const char* uri, const char* content) {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}


int VSDK_Unsubscribe(void* hdl, const char* uri) {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

long long VSDK_GetConsoleHWND() {
	sprintf(stderr, "%s called\n", __FUNCTION__);
	return 0;
}

