#include <stdio.h>
#include <stdarg.h>

#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include "Util.h"

// drm info
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

//------------------------------------------------------------------------------
uint64_t NX_GetTickCount( void )
{
	uint64_t ret;
	struct timeval	tv;
	struct timezone	zv;
	gettimeofday( &tv, &zv );
	ret = ((uint64_t)tv.tv_sec)*1000 + tv.tv_usec/1000;
	return ret;
}

//------------------------------------------------------------------------------
void NX_DumpData( void *data, int32_t len, const char *pFormat, ... )
{
	va_list args;
	va_start(args, pFormat);
	vprintf(pFormat, args);
	va_end(args);

	int32_t i=0;
	uint8_t *byte = (uint8_t *)data;

	if( data == NULL || len == 0 )
		return;

	for( i=0 ; i<len ; i ++ )
	{
		if( i!=0 && i%16 == 0 )	printf("\n\t");
		printf("%.2x", byte[i] );
		if( i%4 == 3 ) printf(" ");
	}
	printf("\n");
}

//------------------------------------------------------------------------------
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, const char *pFormat, ... )
{
	char szFile[1024] = {0x00, };

	va_list args;
	va_start(args, pFormat);
	vsnprintf(szFile, sizeof(szFile), pFormat, args);
	va_end(args);

	FILE *pFile = fopen( szFile, "wb" );
	if( pFile )
	{
		if( pStrmBuf )	fwrite( pStrmBuf, 1, iStrmSize, pFile );
		fclose( pFile );
	}
}

//------------------------------------------------------------------------------
void NX_DumpStream( uint8_t *pStrmBuf, int32_t iStrmSize, FILE *pFile )
{
	if( pFile && pStrmBuf )
	{
		fwrite( pStrmBuf, 1, iStrmSize, pFile );
	}
}

//------------------------------------------------------------------------------
static int32_t find_video_plane( int fd, int crtcIdx, uint32_t *connId, uint32_t *crtcId, uint32_t *planeId )
{
    uint32_t possible_crtcs = 0;
    drmModeRes *res;
    drmModePlaneRes *pr;
    drmModePlane *plane;
    uint32_t i, j;
    int32_t found = 0;

    res = drmModeGetResources(fd);

    if( crtcIdx >= res->count_crtcs )
        goto ErrorExit;

    *crtcId = res->crtcs[ crtcIdx ];
    *connId = res->connectors[ crtcIdx ];

    possible_crtcs = 1<<crtcIdx;

    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    pr = drmModeGetPlaneResources( fd );

    for( i=0 ; i<pr->count_planes ; ++i )
    {
        plane = drmModeGetPlane( fd, pr->planes[i] );
        if( plane->possible_crtcs & possible_crtcs )
        {
            for( j=0 ; j<plane->count_formats ; j++ )
            {
                if( plane->formats[j]==DRM_FORMAT_YUV420 ||
                    plane->formats[j]==DRM_FORMAT_YVU420 ||
                    plane->formats[j]==DRM_FORMAT_UYVY ||
                    plane->formats[j]==DRM_FORMAT_VYUY ||
                    plane->formats[j]==DRM_FORMAT_YVYU ||
                    plane->formats[j]==DRM_FORMAT_YUYV )
                {
                    found = 1;
                    *planeId = plane->plane_id;
                }
            }
        }
    }
    drmModeFreeResources(res);
    return found?0:-1;
ErrorExit:
    drmModeFreeResources(res);
    return -1;
}

static int32_t find_rgb_plane( int fd, int32_t crtcIdx, int32_t layerIdx, uint32_t *connId, uint32_t *crtcId, uint32_t *planeId )
{
    uint32_t possible_crtcs = 0;
    drmModeRes *res;
    drmModePlaneRes *pr;
    drmModePlane *plane;
    uint32_t i, j;
    int32_t found = 0;
    int32_t findIdx = 0;
    int32_t isRgb = 0;

    res = drmModeGetResources(fd);
    if( crtcIdx >= res->count_crtcs )
        goto ErrorExit;

    *crtcId = res->crtcs[ crtcIdx ];
    *connId = res->connectors[ crtcIdx ];

    possible_crtcs = 1<<crtcIdx;

    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    pr = drmModeGetPlaneResources( fd );

    for( i=0 ; i<pr->count_planes ; i++ )
    {
        plane = drmModeGetPlane( fd, pr->planes[i] );
        if( plane->possible_crtcs & possible_crtcs )
        {
            isRgb = 0;
            for( j=0 ; j<plane->count_formats ; j++ )
            {
                if( plane->formats[j]==DRM_FORMAT_ABGR8888 ||
                    plane->formats[j]==DRM_FORMAT_RGBA8888 ||
                    plane->formats[j]==DRM_FORMAT_XBGR8888 ||
                    plane->formats[j]==DRM_FORMAT_RGBX8888 ||
                    plane->formats[j]==DRM_FORMAT_RGB888 ||
                    plane->formats[j]==DRM_FORMAT_BGR888 )
                {
                    isRgb = 1;
                }
            }

            if( isRgb )
            {
                if( findIdx == layerIdx )
                {
                    found = 1;
                    *planeId = plane->plane_id;
                    break;
                }
                findIdx++;
            }
        }
    }
    drmModeFreeResources(res);
    return found?0:-1;
ErrorExit:
    drmModeFreeResources(res);
    return -1;
}


int32_t NX_FindPlaneForDisplay(int32_t crtcIdx,
        	                int32_t findRgb,
            	            int32_t layerIdx,
                	        MP_DRM_PLANE_INFO *pDrmPlaneInfo)
{
    int32_t hDrmFd = -1;
    uint32_t connId = 0;
    uint32_t crtcId = 0;
    uint32_t planeId = 0;

    hDrmFd = drmOpen( "nexell", NULL );

    if( 0 > hDrmFd )
	{
        printf("Fail, drmOpen().\n");
        return -1;
    }

    if( findRgb )
    {
        if( 0 == find_rgb_plane(hDrmFd, crtcIdx, layerIdx, &connId, &crtcId, &planeId) )
        {
		printf( "RGB : connId = %d, crtcId = %d, planeId = %d\n", connId, crtcId, planeId);
        }
        else
        {
            printf("cannot found video format for %dth crtc\n", crtcIdx );
            drmClose( hDrmFd );
            return -1;
        }
    }
    else
    {
        if( 0 == find_video_plane(hDrmFd, crtcIdx, &connId, &crtcId, &planeId) )
        {
	/*
            printf( "VIDEO : connId = %d, crtcId = %d, planeId = %d\n", connId, crtcId, planeId);
	*/
        }
        else
        {
            printf( "cannot found video format for %dth crtc\n", crtcIdx );
            drmClose( hDrmFd );
            return -1;
        }
    }
    drmClose( hDrmFd );

    pDrmPlaneInfo->iConnectorID     = connId;
    pDrmPlaneInfo->iCrtcId        	= crtcId;
    pDrmPlaneInfo->iPlaneId         = planeId;

    return 0;
}

int32_t NX_GetDisplayResolution(uint16_t *width, uint16_t *height)
{
	int32_t hDrmFd = -1;
    	drmModeResPtr res;
	drmModeConnectorPtr con = NULL;
	int32_t ret = -1;

    	hDrmFd = drmOpen("nexell", NULL);

    	res = drmModeGetResources(hDrmFd);

	for (int i = 0; i < res->count_connectors; i++) {
		con = drmModeGetConnector(hDrmFd, res->connectors[i]);

		if (con->connection == DRM_MODE_CONNECTED) {
			ret = 0;
			break;
		}
	}

    	drmClose(hDrmFd);

	if (ret < 0)
		return ret;

	*width = con->modes[0].hdisplay;
	*height = con->modes[0].vdisplay;

	return ret;
}

int32_t NX_SetPlanePropertyByPlaneID(int32_t hDrmFd, uint32_t planeId,
		const char *property, uint32_t value, uint32_t *orgValue)
{
	int ret = -1;
	int32_t prop_id = -1;
	int i;
	drmModeObjectPropertiesPtr properties;

	properties = drmModeObjectGetProperties(hDrmFd, planeId,
			DRM_MODE_OBJECT_PLANE);
	if (!properties)
		goto ERROR;

        for (i = 0; i < (int)properties->count_props; i++) {
                drmModePropertyPtr p;

                p = drmModeGetProperty(hDrmFd, properties->props[i]);
                if (p) {
                        if (strcmp(p->name, "IN_FORMATS") == 0) {
                                drmModeFreeProperty(p);
                                continue;
                        }
#ifdef _DEBUG
			printf("p name = %s, p value = %d\n",
					p->name, properties->prop_values[i]);
#endif
			if (!strcmp(p->name, property)) {
				prop_id = p->prop_id;
				if (orgValue)
					*orgValue =
					(uint32_t)properties->prop_values[i];
				drmModeFreeProperty(p);
				break;
			}
                }
                drmModeFreeProperty(p);
        }
        drmModeFreeObjectProperties(properties);

	if (prop_id > 0)
		ret = drmModeObjectSetProperty(hDrmFd, planeId,
			DRM_MODE_OBJECT_PLANE, prop_id, value);
ERROR:
	return ret;
}



//------------------------------------------------------------------------------
