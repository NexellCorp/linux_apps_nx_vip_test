#ifndef __UTIL_h__
#define __UTIL_h__

#include <stdint.h>

/* Application Data */
typedef struct APP_DATA {
	int32_t width;				/* Input YUV Image Width */
	int32_t height;				/* Input YUV Image Height */
	int32_t cropWidth;			/* Crop YUV Image Width */
	int32_t cropHeight;			/* Crop YUV Image Height */
	int32_t dpWidth;			/* Display YUV Image Width */
	int32_t dpHeight;			/* Display YUV Image Height */

	int32_t dpPort;

	bool bEnableDeinter;
	bool bEnablePreview;
	bool bEnableRatio;

	/* Output Options */
	char *outFileName;			/* Output File Name */
} APP_DATA;

uint64_t NX_GetTickCount( void );
void NX_DumpData( void *data, int32_t len, const char *pFormat, ... );
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, const char *pFormat, ... );
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, FILE *pFile );


typedef struct MP_DRM_PLANE_INFO {
	int32_t		iConnectorID;		//  Dsp Connector ID
	int32_t		iPlaneId;			//  DRM Plane ID
	int32_t		iCrtcId;			//  DRM CRTC ID
} MP_DRM_PLANE_INFO;

int32_t NX_FindPlaneForDisplay(int32_t crtcIdx, int32_t findRgb,
				int32_t layerIdx,
				MP_DRM_PLANE_INFO *pDrmPlaneInfo);
int32_t NX_GetDisplayResolution(uint16_t *width, uint16_t *height);
int32_t NX_SetPlanePropertyByPlaneID(int32_t hDrmFd, uint32_t planeId,
	const char *property, uint32_t value, uint32_t *orgValue);

#endif // __UTIL_h__
