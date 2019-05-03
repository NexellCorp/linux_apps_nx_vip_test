#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include <linux/videodev2.h>
#include <videodev2_nxp_media.h>
#include <media-bus-format.h>
#include <nx_video_alloc.h>
#include <nx_video_api.h>

#include "NX_CV4l2Camera.h"
#include "NX_DeinterlacerManager.h"
#include "NX_V4l2Utils.h"
#include "Util.h"

#include <drm_fourcc.h>
#include "DrmRender.h"

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

#define IMAGE_BUFFER_NUM	8
#define IMG_FORMAT		V4L2_PIX_FMT_YUV420
#define IMG_PLANE_NUM		NX_V4l2GetPlaneNum( IMG_FORMAT )

#define DEINTER_ALIGN	512
#define SRC_H_ALIGN	32
#define SRC_V_ALIGN	16

#define SAVE_FILE	0
#define SAVE_ORG_DATA	0

enum DRM_PLANE_TYPE {
	DRM_PLANE_TYPE_OVERLAY_VIDEO = 0,
	DRM_PLANE_TYPE_OVERLAY_RGB,
};

extern bool bExitLoop;

static void signal_handler( int32_t sig )
{
	printf("Aborted by signal %s (%d)..\n", (char*)strsignal(sig), sig);

	switch( sig )
	{
		case SIGINT :
			printf("SIGINT..\n"); 	break;
		case SIGTERM :
			printf("SIGTERM..\n");	break;
		case SIGABRT :
			printf("SIGABRT..\n");	break;
		default :
			break;
	}

	if( !bExitLoop )
		bExitLoop = true;
	else{
		usleep(1000000);	/* wait 1 seconds for double
					   Ctrl+C operation */
		exit(EXIT_FAILURE);
	}
}

static void register_signal( void )
{
	signal( SIGINT,  signal_handler );
	signal( SIGTERM, signal_handler );
	signal( SIGABRT, signal_handler );
}

static int SaveFile(const char *file, void *buf, long size)
{
	FILE *f;

	if ((f = fopen(file, "wb")) == NULL) {
		printf("failed to save file\n");
		return -1;
	}

	fwrite(buf, 1, size, f);

	if (f)
		fclose(f);

	return 0;
}

static int MakeDir(const char *path)
{
	char self_path[100];
	int ret;

	if (access(path, 0) < 0) {
		ret = mkdir(path, 0x0777);
		if (ret < 0)
			return ret;
	}

	getcwd(self_path, 200);
	if (strcmp(self_path, path) != 0)
		ret = chdir(path);
		if (ret < 0)
			return ret;

	return 0;
}

static int SaveData(const char *file , void *buf, long size)
{
	const char *tPath = "/data";
	int ret;

	ret = MakeDir(tPath);
	if (ret < 0)
		return ret;

	return SaveFile(file, buf, size);
}

static void RemoveStride(int w, int h, int align_factor, void *dst, void *src)
{
	int i;
	uint32_t y_stride, c_stride, h_stride;
	uint32_t w_align, h_align = 16;
	uint32_t y_src_pos, cb_src_pos, cr_src_pos;
	uint32_t y_dst_pos, cb_dst_pos, cr_dst_pos;

	w_align = align_factor;

	y_stride = ALIGN(w, w_align);
	c_stride = ALIGN(w >> 1, w_align >> 1);
	h_stride = ALIGN(h, h_align);

	y_src_pos = 0;
	cb_src_pos = y_src_pos + (y_stride * h_stride);
	cr_src_pos = cb_src_pos + (c_stride * ALIGN(h >> 1, h_align >> 1));

	y_dst_pos = 0;
	cb_dst_pos = y_dst_pos + (w * h);
	cr_dst_pos = cb_dst_pos + ((w >> 1) * (h >> 1));

	for (i = 0; i < h; i++) {
		/* y */
		memcpy((uint8_t*)dst + y_dst_pos, (uint8_t*)src + y_src_pos, w);

		y_src_pos += y_stride;
		y_dst_pos += w;

		if (i < (h >> 1)) {
			/* cb */
			memcpy((uint8_t*)dst + cb_dst_pos,
					(uint8_t*)src + cb_src_pos,
					(w >> 1));
			cb_src_pos += c_stride;
			cb_dst_pos += (w >> 1);

			/* cr */
			memcpy((uint8_t*)dst + cr_dst_pos,
					(uint8_t*)src + cr_src_pos,
					(w >> 1));
			cr_src_pos += c_stride;
			cr_dst_pos += (w >> 1);
		}
	}
}

static void getUsecTime(char *dt)
{
	struct timeval val;
	struct tm *ptm;

	gettimeofday(&val, NULL);
	ptm = localtime(&val.tv_sec);

	memset(dt, 0x00, sizeof(*dt));

	/* format - YYMMDDhhmmssuuuuuu */
	sprintf(dt, "%04d%02d%02d%02d%02d%02d%06ld",
			ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
			ptm->tm_hour, ptm->tm_min, ptm->tm_sec, val.tv_usec);
}

static int32_t VpuCamDpMain( APP_DATA *pAppData )
{
	DRM_DSP_HANDLE hDsp = NULL;
	DRM_RECT srcRect, dstRect;
	int32_t planeID, crtcID;
	NX_VIP_INFO info;
	NX_V4l2_INFO v4l2;
	NX_CV4l2Camera*	pV4l2Camera = NULL;

	bool bEnableDeinter = pAppData->bEnableDeinter;
	bool bEnablePreview = pAppData->bEnablePreview;
	bool bEnableRatio = pAppData->bEnableRatio;

	int32_t alignFactor = 0;
	int32_t frmCnt = 0;

	NX_DeinterlacerManager* manager;
	NX_DEINTER_PARAM deinterParam;
	NX_VID_MEMORY_INFO DeinterMemory[IMAGE_BUFFER_NUM];
	NX_VID_MEMORY_INFO *pDeinterBuf = NULL, *pInputBuf = NULL;

	void *DBuf = NULL;
	int32_t SaveWidth = 0, SaveHeight = 0;
	unsigned long bufSize = 0;

#if SAVE_FILE
	char FileName[200], TimeVal[50];
#endif
	NX_VID_MEMORY_HANDLE hVideoMemory[IMAGE_BUFFER_NUM];

	int32_t ret = 0, i;
	int32_t iPlanes = IMG_PLANE_NUM;
	int32_t inWidth, inHeight, cropWidth, cropHeight;
	uint16_t dpWidth, dpHeight;
	int32_t dpPort;

	char *outFileName = NULL;
	FILE *fpOut = NULL;

	int drmFd;

	if ((pAppData->width == 0) || (pAppData->height == 0)) {
		inWidth = 720;
		inHeight = 480;
	} else {
		inWidth = pAppData->width;
		inHeight = pAppData->height;
	}

	if (bEnableDeinter)
		alignFactor = DEINTER_ALIGN;

	if (pAppData->cropWidth > 0 && pAppData->cropHeight > 0) {
		cropWidth = pAppData->cropWidth;
		cropHeight = pAppData->cropHeight;
	} else {
		cropWidth = 0;
		cropHeight = 0;
	}

	if (pAppData->outFileName)
		outFileName = pAppData->outFileName;

	if (pAppData->dpPort >= 0)
		dpPort = pAppData->dpPort;

	if (outFileName) {
		fpOut = fopen(outFileName, "wb");
		if ( fpOut == NULL) {
			printf("input file or output file open error!!\n");
			goto CAM_DP_TERMINATE;
		}
	}

	register_signal();

	drmFd = open("/dev/dri/card0", O_RDWR);
	if (bEnablePreview) {
		hDsp = CreateDrmDisplay(drmFd);

		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.width = inWidth;
		srcRect.height = inHeight;

		dstRect.x = 0;
		dstRect.y = 0;

		NX_GetDisplayResolution(&dpWidth, &dpHeight);
		if (pAppData->dpWidth > 0 && pAppData->dpHeight > 0) {
			dstRect.width = pAppData->dpWidth;
			dstRect.height = pAppData->dpHeight;
		} else {
			dstRect.width = dpWidth;
			dstRect.height = dpHeight;
		}
	}

	if (bEnableRatio) {
		uint32_t ratWidth;
		uint32_t ratHeight;

		if ((cropWidth > 0 && inWidth > cropWidth)
		 && (cropHeight > 0 && inHeight > cropHeight)) {
			ratWidth = cropWidth;
			ratHeight = cropHeight;
		} else {
			ratWidth = inWidth;
			ratHeight = inHeight;
		}

		double xRatio = (double)dpWidth/(double)ratWidth;
		double yRatio = (double)dpHeight/(double)ratHeight;

		if (xRatio > yRatio) {
			dstRect.width = ratWidth * yRatio;
			dstRect.height = dpHeight;
			dstRect.x = abs(dpWidth - (ratWidth * yRatio))
				/ 2;
		} else {
			dstRect.width = dpWidth;
			dstRect.height = ratHeight * xRatio;
			dstRect.y = abs(dpHeight - (ratHeight * xRatio))
				/ 2;
		}
	}

	memset( &info, 0x00, sizeof(info) );

	info.iModule		= 0;
	info.iSensorId		= nx_sensor_subdev;
	info.bUseMipi		= false;
	info.iWidth		= inWidth;
	info.iHeight		= inHeight;
	info.iFpsNum		= 30;
	info.iNumPlane		= 1;

	if ((cropWidth > 0) && (cropHeight > 0) && (inWidth > cropWidth
			 || inHeight > cropHeight)) {
		info.iCropX		= (inWidth - cropWidth) / 2;
		info.iCropY		= (inHeight - cropHeight) / 2;
		info.iCropWidth		= cropWidth;
		info.iCropHeight	= cropHeight;
	} else {
		info.iCropX		= 0;
		info.iCropY		= 0;
		info.iCropWidth		= inWidth;
		info.iCropHeight	= inHeight;
	}

	info.iOutWidth		= inWidth;
	info.iOutHeight		= inHeight;

	v4l2.pixelFormat	= V4L2_PIX_FMT_YUV420;
	v4l2.busFormat 		= MEDIA_BUS_FMT_UYVY8_2X8;

	pV4l2Camera = new NX_CV4l2Camera();
	if( 0 > pV4l2Camera->Init( &info, &v4l2 ) )
	{
		delete pV4l2Camera;
		pV4l2Camera = NULL;

		printf( "Fail, V4l2Camera Init().\n");
		goto CAM_DP_TERMINATE;
	}

	for ( i = 0; i < IMAGE_BUFFER_NUM; i++) {
		hVideoMemory[i] = (NX_VID_MEMORY_INFO*)malloc(
				sizeof(NX_VID_MEMORY_INFO));
		memset(hVideoMemory[i], 0, sizeof(NX_VID_MEMORY_INFO));
		pV4l2Camera->SetVideoMemory( hVideoMemory[i] );
	}

	if (info.iCropWidth > 0 && info.iCropHeight > 0) {
		srcRect.width = info.iCropWidth;
		srcRect.height = info.iCropHeight;
	}

	MP_DRM_PLANE_INFO DrmPlaneInfo;
	NX_FindPlaneForDisplay(dpPort, DRM_PLANE_TYPE_OVERLAY_VIDEO, 0,
			&DrmPlaneInfo);
	planeID = DrmPlaneInfo.iPlaneId;
	crtcID = DrmPlaneInfo.iCrtcId;

	if (bEnablePreview)
		InitDrmDisplay(hDsp, planeID, crtcID, DRM_FORMAT_YUV420,
				srcRect, dstRect,  alignFactor);

	if (bEnableDeinter) {
		deinterParam.srcWidth = inWidth;
		deinterParam.srcHeight = inHeight;
		deinterParam.cropWidth = info.iCropWidth;
		deinterParam.cropHeight = info.iCropHeight;
		deinterParam.format = V4L2_PIX_FMT_YUV420;
		deinterParam.planes = 1;
		deinterParam.bufNum = IMAGE_BUFFER_NUM;

		manager = new NX_DeinterlacerManager(deinterParam);

		ret = manager->DeinterCreateBuffer();
		if (ret < 0) {
			printf("failed allocation for deinterlacen");
			goto CAM_DP_TERMINATE;
		}

		for (i = 0; i < IMAGE_BUFFER_NUM; i++) {
			manager->GetBuffer(&DeinterMemory[i], i);
			pInputBuf = &DeinterMemory[i];
			manager->qDstBuf(pInputBuf);
		}
	}

	SaveWidth = srcRect.width;
	SaveHeight = srcRect.height;

	bufSize = (SaveWidth * SaveHeight) +
		(2 * ((SaveWidth >> 1) * (SaveHeight >> 1)));
	DBuf = calloc(1, bufSize);
	if (!DBuf) {
		printf("failed to allocation deinterlacer buffer.\n");
		goto CAM_DP_TERMINATE;
	}

	while (!bExitLoop) {
		NX_VID_MEMORY_INFO *pBuf = NULL;
		NX_VID_MEMORY_INFO *pTBuf = NULL;
		NX_VID_MEMORY_INFO *pSrcBuf = NULL;
		NX_VID_MEMORY_INFO *pDeinterBuf = NULL;
		NX_VID_MEMORY_INFO *pSaveBuf = NULL;
		int32_t BufferIndex = 0;
		int32_t BufferDeinterIndex = 0;

		ret = pV4l2Camera->DequeueBuffer(&BufferIndex, &pBuf);
		if (0 >  ret) {
			printf( "Fail, DequeueBuffer().\n" );
			break;
		}

		if (bEnableDeinter) {
			pTBuf = hVideoMemory[BufferIndex];
			manager->qSrcBuf(BufferIndex, pTBuf);

			printf("Deinter Buf Index = %d\n", frmCnt);

			if (manager->Run()) {
				for (i = 0; i < manager->getRunCount(); i++) {
					manager->dqDstBuf(&pDeinterBuf);
					pBuf = pDeinterBuf;
					pSaveBuf = pBuf;
#if SAVE_FILE
#if !SAVE_ORG_DATA
					getUsecTime(TimeVal);
					RemoveStride(SaveWidth,
						SaveHeight,
						DST_ALIGN,
						DBuf,
						pBuf->pBuffer[0]);
					sprintf(FileName,
						"out_%d_%s.yuv",
						frmCnt, TimeVal);
					SaveData(FileName, DBuf,
							bufSize);

#else
					int bufsize = (ALIGN(SaveWidth,
						SRC_H_ALIGN)
						* ALIGN(SaveHeight,
						SRC_V_ALIGN))
						+ (2 *
						((ALIGN(SaveWidth >> 1,
						DST_ALIGN) *
						ALIGN(SaveHeight >> 1,
						16))));
					sprintf(FileName,
						"data_dst_%d_%s.yuv",
						BufferIndex, TimeVal);
					SaveData(FileName,
						pBuf->pBuffer[0],
						bufsize);
#endif
#endif
					if (bEnablePreview)
						UpdateBuffer(hDsp, pBuf, NULL);

					manager->qDstBuf(pDeinterBuf);
				}
			}

			if (manager->dqSrcBuf(&BufferDeinterIndex, &pSrcBuf)) {
				pBuf = pSrcBuf;
				if (0 > pV4l2Camera->QueueBuffer(pBuf)) {
					printf("Fail, DequeueBuffer().\n");
					break;
				}
			}

			frmCnt++;
		} else {
			printf("VIP Buf Index = %d\n", BufferIndex);
			pSaveBuf = pBuf;

			if (bEnablePreview)
				UpdateBuffer(hDsp, pBuf, NULL);

			if (0 > pV4l2Camera->QueueBuffer(pBuf)) {
				printf("Fail, DequeueBuffer().\n");
				break;
			}

			frmCnt++;
		}

		if (outFileName) {
			if (alignFactor == 0)
				alignFactor = 32;

			ret = MakeDir("/data");
			if ((ret >= 0) && (pSaveBuf))  {
				RemoveStride(SaveWidth, SaveHeight,
					alignFactor, DBuf,
					pSaveBuf->pBuffer[0]);
				if (fpOut)
					fwrite((void *)DBuf, 1, bufSize,
						fpOut);
			}
		}
	}/* while end */

	if (DBuf) {
		free(DBuf);
		DBuf = NULL;
	}

CAM_DP_TERMINATE:
	if ( pV4l2Camera ) {
		pV4l2Camera->Deinit();
		delete pV4l2Camera;
		pV4l2Camera = NULL;
	}

	for ( i = 0; i < IMAGE_BUFFER_NUM; i++ ) {
		if ( hVideoMemory[i] ) {
			free( hVideoMemory[i] );
			hVideoMemory[i] = NULL;
		}
	}

	if (bEnableDeinter) {
		if (manager) {
			delete manager;
			manager = NULL;
		}
	}

	if (fpOut)
		fclose(fpOut);

	printf("Camera Test End!!\n" );

	return ret;
}

int32_t VpuCamMain(APP_DATA *pAppData)
{
	return VpuCamDpMain(pAppData);
}
