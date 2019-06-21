#include "NX_CV4l2Camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#include <videodev2_nxp_media.h>
#include <media-bus-format.h>
#include <nx-drm-allocator.h>
#include <unistd.h>

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

NX_CV4l2Camera::NX_CV4l2Camera()
	: m_hV4l2		(NULL)
	, m_iCurQueuedSize(0)
	, iQSlotIndex(0)
	, iDQSlotIndex(0)
	, m_bufNum(0)
{
	for (int32_t i = 0; i < MAX_BUF_NUM; i++) {
		m_pMemSlot[i] = NULL;
	}

	pthread_mutex_init(&m_hLock, NULL);
}

NX_CV4l2Camera::~NX_CV4l2Camera()
{
	pthread_mutex_destroy(&m_hLock);
}

int32_t NX_CV4l2Camera::V4l2CameraInit(NX_V4l2_INFO *pInfo, int32_t bUseMipi)
{
	int32_t ret = 0, i = 0;

	ret = V4l2OpenDevices(pInfo);
	if (-1 == ret) {
		printf("Fail, V4l2OpenDevices().\n");
		return -1;
	}

	pInfo->cameraBufNum = CAMERA_BUF_NUM;

	ret = V4l2Link(pInfo);
	if (-1 == ret) {
		printf( "Fail, V4l2Link().\n" );
		return -1;
	}

	ret = V4l2SetFormat(pInfo, bUseMipi);
	if (-1 == ret) {
		printf("Fail, V4l2SetFormat().\n");
		return -1;
	}

	ret = V4l2CreateBuffer(pInfo);
	if (ret == -1) {
		printf("Fail, V4l2CreateBuffer().\n");
		return -1;
	}

	ret = nx_v4l2_reqbuf(pInfo->clipperVideoFd, nx_clipper_video,
						pInfo->cameraBufNum);
	if (ret) {
		printf("failed to reqbuf\n");
		return -1;
	}

	for (i = 0; i < pInfo->cameraBufNum; i++) {
		ret = nx_v4l2_qbuf(pInfo->clipperVideoFd,
				nx_clipper_video, m_hV4l2->numPlane, i,
				&pInfo->dmaFds[i],
				(int32_t *)&pInfo->cameraBufSize);
		if (ret) {
			printf("failed to qbuf: index %d\n", i);
			return -1;
		}
	}

	m_bufNum = pInfo->cameraBufNum;

	ret = nx_v4l2_streamon(pInfo->clipperVideoFd, nx_clipper_video);
	if (ret) {
		printf("failed to streamon.\n");
		return -1;
	}

	return 0;
}

int32_t NX_CV4l2Camera::V4l2OpenDevices(NX_V4l2_INFO *pInfo)
{
	pInfo->sensorFd = nx_v4l2_open_device(m_hV4l2->sensorId, pInfo->module);
	if (pInfo->sensorFd < 0) {
		printf( "Fail, nx_v4l2_open_device(nx_sensor_subdev).\n" );
		return -1;
	}

	pInfo->clipperSubdevFd = nx_v4l2_open_device(nx_clipper_subdev,
			pInfo->module);
	if (pInfo->clipperSubdevFd < 0) {
		printf( "Fail, nx_v4l2_open_device(nx_clipper_subdev).\n" );
		return -1;
	}

	pInfo->clipperVideoFd = nx_v4l2_open_device(nx_clipper_video,
			pInfo->module);
	if (pInfo->clipperVideoFd < 0) {
		printf( "Fail, nx_v4l2_open_device(clipper_video).\n" );
		return -1;
	}

	pInfo->bIsMipi = nx_v4l2_is_mipi_camera(pInfo->module);
	if (pInfo->bIsMipi) {
		pInfo->csiSubdevFd = nx_v4l2_open_device(nx_csi_subdev,
				pInfo->module);
		if (pInfo->csiSubdevFd < 0) {
			printf( "Failed to open mipi csi.\n" );
			return -1;
		}
	}

	return 0;
}

int32_t NX_CV4l2Camera::V4l2Link(NX_V4l2_INFO *pInfo)
{
	int32_t ret = 0;

	/* link */
	ret = nx_v4l2_link(true, pInfo->module, nx_clipper_subdev, 1,
						nx_clipper_video, 0);
	if (ret) {
		printf( "failed to link clipper_sub -> clipper_video.\n" );
		return -1;
	}

	if (pInfo->bIsMipi) {
		ret = nx_v4l2_link(true, pInfo->module, nx_sensor_subdev, 0,
						nx_csi_subdev, 0);
		if (ret) {
			printf( "failed to link sensor -> mipi_csi.\n" );
			return -1;
		}

		ret = nx_v4l2_link(true, pInfo->module, nx_csi_subdev, 1,
						nx_clipper_subdev, 0);
		if (ret) {
			printf( "failed to link mipi_csi -> clipper_sub.\n" );
			return -1;
		}
	} else {
		ret = nx_v4l2_link(true, pInfo->module, nx_sensor_subdev, 0,
						nx_clipper_subdev, 0);
		if (ret) {
			printf( "failed to link sensor -> clipper_sub.\n" );
			return -1;
		}
	}

	return 0;
}

int32_t NX_CV4l2Camera::V4l2SetFormat(NX_V4l2_INFO *pInfo, int32_t bUseMipi)
{
	int32_t ret = 0;

	ret = nx_v4l2_set_format(m_hV4l2->sensorFd, nx_sensor_subdev,
			m_hV4l2->width, m_hV4l2->height, m_hV4l2->busFormat);
	if (ret) {
		printf( "failed to set_format for sensor.\n" );
		return -1;
	}

	if (bUseMipi) {
		if (m_hV4l2->bIsMipi) {
			ret = nx_v4l2_set_format(m_hV4l2->csiSubdevFd,
					nx_csi_subdev, m_hV4l2->width,
					m_hV4l2->height, m_hV4l2->pixelFormat);
			if (ret) {
				printf( "failed to set_format for mipi_csi.\n" );
				return -1;
			}
		}
	}

	ret = nx_v4l2_set_format(m_hV4l2->clipperSubdevFd, nx_clipper_subdev,
			m_hV4l2->width, m_hV4l2->height, m_hV4l2->busFormat);
	if (ret) {
		printf( "failed to set_format for clipper_subdev.\n" );
		return -1;
	}

	ret = nx_v4l2_set_format(m_hV4l2->clipperVideoFd, nx_clipper_video,
			m_hV4l2->width, m_hV4l2->height, m_hV4l2->pixelFormat);
	if (ret) {
		printf( "failed to set_format for clipper_subdev.\n" );
		return -1;
	}

	if (m_hV4l2->cropWidth > 0 && m_hV4l2->cropHeight > 0) {
		ret = nx_v4l2_set_crop(m_hV4l2->clipperSubdevFd,
				nx_clipper_subdev,
				m_hV4l2->cropX, m_hV4l2->cropY,
				m_hV4l2->cropWidth,
				m_hV4l2->cropHeight);
		if (ret) {
			printf("failed to set_format for clipper_subdev.\n");
			return -1;
		}
	} else {
		ret = nx_v4l2_set_crop(m_hV4l2->clipperSubdevFd,
				nx_clipper_subdev, 0, 0, m_hV4l2->width,
				m_hV4l2->height);
		if (ret) {
			printf( "failed to set_crop for clipper_subdev.\n" );
			return -1;
		}
	}

	return 0;
}

int32_t	NX_CV4l2Camera::V4l2CreateBuffer(NX_V4l2_INFO *pInfo)
{
	int32_t drmFd = 0;
	int32_t gemFd = 0;
	int32_t dmaFd = 0;
	void *pVaddr = NULL;
	int32_t i = 0;

	int32_t Width;
	int32_t Height;

	drmFd = open_drm_device();
	if (drmFd < 0) {
		printf( "failed to open drm device.\n" );
		return -1;
	}
	pInfo->drmFd = drmFd;

	int32_t allocSize = V4l2CalcAllocSize(pInfo->width, pInfo->height,
			pInfo->pixelFormat);
	if (allocSize <= 0) {
		printf( "invalid alloc size %d\n", allocSize );
		return -1;
	}
	pInfo->cameraBufSize = allocSize;

	for (i = 0; i < pInfo->cameraBufNum; i++) {
		gemFd = alloc_gem(drmFd, allocSize, 0);
		if (gemFd < 0) {
			printf( "failed to alloc gem %d\n", i );
			return -1;
		}

		dmaFd = gem_to_dmafd(drmFd, gemFd);
		if (dmaFd < 0) {
			printf( "failed to gem to dma %d\n", i );
			return -1;
		}

		if (get_vaddr(drmFd, gemFd, allocSize, &pVaddr)) {
			printf( "failed to get_vaddr %d\n", i);
			return -1;
		}

		pInfo->gemFds[i] = gemFd;
		pInfo->dmaFds[i] = dmaFd;
		pInfo->pVaddr[i] = pVaddr;
	}

	return 0;
}

int32_t	NX_CV4l2Camera::V4l2CalcAllocSize(uint32_t width, uint32_t height,
		uint32_t format)
{
	uint32_t yStride = ALIGN(width, 32);
	uint32_t ySize = yStride * ALIGN(height, 16);
	int32_t size = 0;

	switch (format) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		size = ySize << 1;
		break;

	case V4L2_PIX_FMT_YUV420:
		size = ySize +
			2 * (ALIGN(yStride >> 1, 16) * ALIGN(height >> 1, 16));
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		size = ySize + yStride * ALIGN(height >> 1, 16);
		break;
	}

	return size;
}

int32_t NX_CV4l2Camera::Init(NX_VIP_INFO *pInfo, NX_V4l2_INFO *pV4l2)
{
	int32_t ret = 0;

	m_hV4l2 = (NX_V4l2_INFO *)malloc(sizeof(NX_V4l2_INFO));
	memset( m_hV4l2, 0x00, sizeof(NX_V4l2_INFO) );
	m_hV4l2->width = pInfo->iWidth;
	m_hV4l2->height = pInfo->iHeight;
	m_hV4l2->pixelFormat = pV4l2->pixelFormat;
	m_hV4l2->busFormat = pV4l2->busFormat;
	m_hV4l2->module = pInfo->iModule;
	m_hV4l2->numPlane = pInfo->iNumPlane;
	m_hV4l2->sensorId = pInfo->iSensorId;
	m_hV4l2->cropX = pInfo->iCropX;
	m_hV4l2->cropY = pInfo->iCropY;
	m_hV4l2->cropWidth = pInfo->iCropWidth;
	m_hV4l2->cropHeight = pInfo->iCropHeight;

	ret = V4l2CameraInit(m_hV4l2, pInfo->bUseMipi);
	if (-1 == ret) {
		printf( "Fail, V4l2Init().\n" );
		return -1;
	}

	return 0;
}

void NX_CV4l2Camera::Deinit(void)
{
	if (m_hV4l2) {
		if(m_hV4l2)
			V4l2Deinit(m_hV4l2);

		m_iCurQueuedSize= 0;

		for (int32_t i = 0; i < MAX_BUF_NUM; i++)
			m_pMemSlot[i] = NULL;

		free(m_hV4l2);
		m_hV4l2 = NULL;
	}
}

void NX_CV4l2Camera::V4l2Deinit(NX_V4l2_INFO *pInfo)
{
	int32_t i = 0;

	nx_v4l2_streamoff(pInfo->clipperVideoFd, nx_clipper_video);
	nx_v4l2_reqbuf(pInfo->clipperVideoFd, nx_clipper_video, 0);

	for (i = 0; i < pInfo->cameraBufNum; i++) {
		close(pInfo->dmaFds[i]);
		close(pInfo->gemFds[i]);
		pInfo->dmaFds[i] = -1;
		pInfo->gemFds[i] = -1;
	}
}

int32_t NX_CV4l2Camera::QueueBuffer( NX_VID_MEMORY_INFO *pVidMem)
{
	int32_t i;
	int32_t iRet = 0;

	pthread_mutex_lock( &m_hLock );

	if(m_iCurQueuedSize >= MAX_BUF_NUM) {
		pthread_mutex_unlock( &m_hLock );
		return -1;
	}

	if (i == MAX_BUF_NUM) {
		printf( "Fail, Have no empty slot.\n" );
		pthread_mutex_unlock( &m_hLock );
		return -1;
	}

	m_iCurQueuedSize++;

	iRet = nx_v4l2_qbuf(m_hV4l2->clipperVideoFd, nx_clipper_video,
			m_hV4l2->numPlane,
			iQSlotIndex,
			&m_hV4l2->dmaFds[iQSlotIndex],
			(int32_t *)&m_hV4l2->cameraBufSize);
	if (0 > iRet) {
		m_pMemSlot[iQSlotIndex] = NULL;
		printf( "Fail, nx_v4l2_qbuf().\n" );
		pthread_mutex_unlock( &m_hLock );
		return iRet;
	}

	iQSlotIndex = (iQSlotIndex  == (m_bufNum -1)) ? 0 : (iQSlotIndex + 1);

	pthread_mutex_unlock( &m_hLock );

	return 0;
}

int32_t NX_CV4l2Camera::DequeueBuffer(int32_t *pBufferIndex,
		NX_VID_MEMORY_INFO **ppVidMem)
{
	int32_t iRet = 0;
	int32_t iSlotIndex = -1;

	pthread_mutex_lock( &m_hLock );

	if (m_iCurQueuedSize < 1) {
		pthread_mutex_unlock( &m_hLock );
		return -1;
	}
	pthread_mutex_unlock( &m_hLock );

	iRet = nx_v4l2_dqbuf(m_hV4l2->clipperVideoFd, nx_clipper_video,
			m_hV4l2->numPlane, &iDQSlotIndex);

	if (0 > iRet) {
		printf("Fail, nx_v4l2_dqbuf().\n");
		return iRet;
	}

	*ppVidMem = m_pMemSlot[iDQSlotIndex];

	if (*ppVidMem == NULL) {
		printf( "Fail, Buffer Error.\n" );
		iRet = -1;
		return iRet;
	}

	NX_VID_MEMORY_INFO *pVidMemInfo = *ppVidMem;
	for (int32_t i = 0; i < m_hV4l2->numPlane; i++) {
		pVidMemInfo->width = m_hV4l2->width;
		pVidMemInfo->height = m_hV4l2->height;
		pVidMemInfo->planes = m_hV4l2->numPlane;
		pVidMemInfo->format = m_hV4l2->pixelFormat;
		pVidMemInfo->size[i] = m_hV4l2->cameraBufSize;
		pVidMemInfo->pBuffer[i] = m_hV4l2->pVaddr[iDQSlotIndex];
		pVidMemInfo->drmFd = m_hV4l2->drmFd;
		pVidMemInfo->dmaFd[i] = m_hV4l2->dmaFds[iDQSlotIndex];
		pVidMemInfo->gemFd[i] = m_hV4l2->gemFds[iDQSlotIndex];
		pVidMemInfo->flink[i] = get_flink_name(m_hV4l2->drmFd, m_hV4l2->gemFds[iDQSlotIndex]);
	}
	pVidMemInfo->stride[0] = ALIGN(m_hV4l2->width, 32);

	*pBufferIndex = iDQSlotIndex;
	pthread_mutex_lock( &m_hLock );
	m_iCurQueuedSize--;

	iDQSlotIndex = (iDQSlotIndex  == (m_bufNum -1)) ? 0 : (iDQSlotIndex + 1);
	pthread_mutex_unlock( &m_hLock );

	return iRet;
}

int32_t NX_CV4l2Camera::SetVideoMemory(NX_VID_MEMORY_INFO *pVidMem)
{
	int32_t i = 0;

	pthread_mutex_lock( &m_hLock );

	if (m_iCurQueuedSize >= MAX_BUF_NUM) {
		pthread_mutex_unlock(&m_hLock);
		return -1;
	}

	for (i = 0; i < MAX_BUF_NUM; i++) {
		if (m_pMemSlot[i] == NULL) {
			m_pMemSlot[i] = pVidMem;
			break;
		}
	}

	if (i == MAX_BUF_NUM) {
		printf("Fail, Have no empty slot.\n");
		pthread_mutex_unlock( &m_hLock );
		return -1;
	}
	m_iCurQueuedSize++;

	pthread_mutex_unlock(&m_hLock);

	return 0;
}
