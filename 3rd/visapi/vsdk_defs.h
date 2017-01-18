#ifndef __VSDK_DEFS_H__
#define __VSDK_DEFS_H__
#ifdef _WIN32
#ifdef VSDK_EXPORTS
#define VSDK_API __declspec(dllexport)
#else
#define VSDK_API __declspec(dllimport)
#endif
#else
#define VSDK_API 
#define __stdcall
#endif

#ifdef __cplusplus
extern "C" {
#endif
	typedef enum vsdk_data_type
	{
		vdt_NONE = 0,
		vdt_XML = 1,
		vdt_JSON = 2,
		vdt_TEXT = 3,
		vdt_MEDIASAMPE = 4
	}vsdk_data_type_t;

	typedef enum vsdk_callback_data_source
	{
		vcds_EVENT = 0,
		vcds_SUBSCRIBE,
		vcds_MEDIA
	}vsdk_callback_data_source_t;

	typedef enum vsdk_request_type
	{
		rt_GET,
		rt_INSERT,
		rt_UPDATE,
		rt_DELETE
	} vsdk_request_type_t;

	typedef struct vsdk_callback_data_header
	{
		int src_type; // 0: event; 1: subscribe   // 2: media 
		int data_type;
		union
		{
			struct
			{
				char event_type[128];
				char event_source[128];
			}event_header;
			char src_uri[256];
			char session_id[256];
		};
	}vsdk_callback_data_header_t;

	typedef void(__stdcall * fp_data_callback)(void* hdl, vsdk_callback_data_header_t* dataHeader, void* dataPtr, int dataSize, void* userData, void* reserved0);
#ifdef __cplusplus
}
#endif
#endif
