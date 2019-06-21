//------------------------------------------------------------------------------
//
//	Copyright (C) 2016 Nexell Co. All Rights Reserved
//	Nexell Co. Proprietary & Confidential
//
//	NEXELL INFORMS THAT THIS CODE AND INFORMATION IS PROVIDED "AS IS" BASE
//  AND	WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING
//  BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS
//  FOR A PARTICULAR PURPOSE.
//
//	Module		:
//	File		:
//	Description	:
//	Author		:
//	Export		:
//	History		:
//
//------------------------------------------------------------------------------

#ifndef __NX_CV4L2CAMERA_H__
#define __NX_CV4L2CAMERA_H__

#ifdef __cplusplus

#include <stdint.h>
#include <pthread.h>

#include <nx_video_api.h>
#include <nx-v4l2.h>
#include <nx-drm-allocator.h>

enum
{
	NX_VIP_NOP		= -1,
};

typedef struct _NX_VIP_INFO
{
	int32_t		iModule;
	int32_t		iSensorId;
	int32_t		bUseMipi;

	int32_t		iWidth;			/* Camera Input Width */
	int32_t		iHeight;		/* Camera Input Height */

	int32_t		iFpsNum;		/* Frame per seconds's
						   Numerate value */

	int32_t 	iNumPlane;		/* Input image's plane number */

	int32_t		iCropX;			/* Cliper x */
	int32_t		iCropY;			/* Cliper y */
	int32_t		iCropWidth;		/* Cliper width */
	int32_t		iCropHeight;		/* Cliper height */

	int32_t		iOutWidth;		/* Decimator width */
	int32_t		iOutHeight;		/* Decimator height */
} NX_VIP_INFO;

#define CAMERA_BUF_NUM	8
typedef struct _NX_V4l2_INFO
{
	int32_t		module;
	int32_t		numPlane;
	int32_t		sensorId;

	int32_t		sensorFd;
	int32_t		clipperSubdevFd;
	int32_t		decimatorSubdevFd;
	int32_t		csiSubdevFd;
	int32_t		clipperVideoFd;
	int32_t		decimatorVideoFd;

	int32_t		bIsMipi;
	int32_t		width;
	int32_t		height;
	int32_t		pixelFormat;
	int32_t		busFormat;

	int32_t		drmFd;
	int32_t		gemFds[CAMERA_BUF_NUM];
	int32_t		dmaFds[CAMERA_BUF_NUM];
	int32_t		cameraBufNum;
	int32_t		cameraBufSize;
	void* 		pVaddr[CAMERA_BUF_NUM];

	// crop attribute
	uint32_t	cropX;
	uint32_t	cropY;
	uint32_t	cropWidth;
	uint32_t	cropHeight;

} NX_V4l2_INFO;

class NX_CV4l2Camera
{
public:
	NX_CV4l2Camera();
	~NX_CV4l2Camera();

public:
	int32_t	Init( NX_VIP_INFO *pInfo, NX_V4l2_INFO *pV4l2 );
	void 	Deinit( void );
	int32_t	QueueBuffer( NX_VID_MEMORY_INFO *pVidMem );
	int32_t DequeueBuffer( int32_t *pBufferIndex, NX_VID_MEMORY_INFO **ppVidMem );
	int32_t SetVideoMemory( NX_VID_MEMORY_INFO *pVidMem );

private:
	int32_t	V4l2CameraInit( NX_V4l2_INFO *pInfo, int32_t bUseMipi );
	int32_t	V4l2OpenDevices( NX_V4l2_INFO *pInfo );
	int32_t	V4l2Link( NX_V4l2_INFO *pInfo );
	int32_t	V4l2SetFormat( NX_V4l2_INFO *pInfo, int32_t bUseMipi );
	int32_t	V4l2CreateBuffer( NX_V4l2_INFO *pInfo );
	int32_t	V4l2CalcAllocSize(uint32_t width, uint32_t height, uint32_t format);
	void	V4l2Deinit( NX_V4l2_INFO *pInfo );

private:
	enum {	MAX_BUF_NUM = 32 };

	NX_V4l2_INFO			*m_hV4l2;
	NX_VID_MEMORY_INFO		*m_pMemSlot[MAX_BUF_NUM];
	int32_t				m_iCurQueuedSize;
	pthread_mutex_t			m_hLock;
	int32_t				iQSlotIndex, iDQSlotIndex;
	int32_t				m_bufNum;

private:
	NX_CV4l2Camera (NX_CV4l2Camera &Ref);
	NX_CV4l2Camera &operator=(NX_CV4l2Camera &Ref);
};

#endif	// __cplusplus
#endif	// __NX_CV4L2CAMERA_H__
