#include "NX_DeinterlacerManager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <nx-drm-allocator.h>

#define DEINTERLACER_DEVICE_NODE_NAME	"/dev/nx-deinterlacer"
#define BUFFER_COUNT	4
#define DEINTER_ALIGN	512
#define SRC_ALIGN	32

#define _DEBUG	0
#define DEINTER_MODE	DOUBLE_FRAME /* or SINGLE_FRAME */

NX_DeinterlacerManager::NX_DeinterlacerManager(struct NX_DEINTER_PARAM param)
	: mSrcWidth(param.srcWidth), mSrcHeight(param.srcHeight),
	mCropWidth(param.cropWidth), mCropHeight(param.cropHeight),
	mPixelFormat(param.format), mPlanes(param.planes),
	mBufNum(param.bufNum)
{
	int Width, Height;

	mRunCount = 0;
	mHandle = -1;
	mDrmHandle = -1;

	mDeinterMode = DEINTER_MODE;
	mCurrentField = FIELD_EVEN;
	mSrcType = SRC_TYPE_PARALLEL;

	NX_InitQueue(&mSrcBufferQueue, mBufNum);
	NX_InitQueue(&mDstBufferQueue, mBufNum);

	if (mCropWidth > 0 && mCropHeight > 0) {
		Width = mCropWidth;
		Height = mCropHeight;
	} else {
		Width = mSrcWidth;
		Height = mSrcHeight;
	}

	mDeinterInfo.width = Width;
	mDeinterInfo.height = Height;
	mDeinterInfo.planes = mPlanes;
	mDeinterInfo.pixelFormat = mPixelFormat;
	mDeinterInfo.deinterBufNum = 0;
}

NX_DeinterlacerManager::~NX_DeinterlacerManager(void)
{
	if (mHandle >= 0)
		close(mHandle);

	if (mDrmHandle >= 0)
		close(mDrmHandle);

	NX_DeinitQueue(&mSrcBufferQueue);
	NX_DeinitQueue(&mDstBufferQueue);
}

int NX_DeinterlacerManager::CalcAllocSize(int width, int height, int format)
{
	int yStride = ALIGN(width, DEINTER_ALIGN);
	int ySize = yStride * ALIGN(height, 16);
	int size = 0;

	switch (format) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		size = ySize << 1;
		break;

	case V4L2_PIX_FMT_YUV420:
		size = ySize +
			(2 * (ALIGN(width >> 1, DEINTER_ALIGN >> 1)
				* ALIGN(height >> 1, 16)));
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		size = ySize + yStride * ALIGN(height >> 1, 16);
		break;
	}

	return size;
}

int NX_DeinterlacerManager::DeinterCreateBuffer(void)
{
	int drmFd = 0;
	int gemFd = 0;
	int dmaFd = 0;
	void *pVaddr = NULL;
	int i = 0;

	drmFd = open_drm_device();
	if (drmFd < 0) {
		printf( "failed to open drm device.\n" );
		return -1;
	}
	mDeinterInfo.drmFd = drmFd;

	int allocSize = CalcAllocSize(mDeinterInfo.width, mDeinterInfo.height,
			mDeinterInfo.pixelFormat);
	if (allocSize <= 0) {
		printf( "invalid alloc size %d\n", allocSize );
		return -1;
	}
	mDeinterInfo.deinterBufSize = allocSize;

	if (!mDeinterInfo.deinterBufNum)
		mDeinterInfo.deinterBufNum = DEINTER_BUF_NUM;

	for (i = 0; i < mDeinterInfo.deinterBufNum; i++) {
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

		if (get_vaddr(drmFd, gemFd, mDeinterInfo.deinterBufSize, &pVaddr)) {
			printf( "failed to get_vaddr %d\n", i);
			return -1;
		}

		mDeinterInfo.gemFds[i] = gemFd;
		mDeinterInfo.dmaFds[i] = dmaFd;
		mDeinterInfo.pVaddr[i] = pVaddr;
#if _DEBUG

		printf("gem Fd=%d, dma Fd=%d, pVaddr=%p, alloc size=%d\n",
				gemFd, dmaFd, pVaddr, allocSize);
#endif
	}

	return 0;
}

int NX_DeinterlacerManager::DeinterReleaseBuffer(void)
{
	int i;

	for (i = 0; i < mDeinterInfo.deinterBufNum; i++) {
		close(mDeinterInfo.dmaFds[i]);
		close(mDeinterInfo.gemFds[i]);
		mDeinterInfo.dmaFds[i] = -1;
		mDeinterInfo.gemFds[i] = -1;
		mDeinterInfo.pVaddr[i] = NULL;
	}
}


bool NX_DeinterlacerManager::GetBuffer(NX_VID_MEMORY_INFO *memInfo, int index)
{
	memInfo->width = mDeinterInfo.width;
	memInfo->height = mDeinterInfo.height;
	memInfo->planes = mDeinterInfo.planes;
	memInfo->format = mDeinterInfo.pixelFormat;
	memInfo->drmFd = mDeinterInfo.drmFd;

	for (int i = 0; i < mDeinterInfo.planes; i++) {
		memInfo->size[i] = mDeinterInfo.deinterBufSize;
		memInfo->pBuffer[i] = mDeinterInfo.pVaddr[index];
		memInfo->dmaFd[i] = mDeinterInfo.dmaFds[index];
		memInfo->gemFd[i] = mDeinterInfo.gemFds[index];
		memInfo->flink[i] = get_flink_name(mDeinterInfo.drmFd,
				mDeinterInfo.gemFds[index]);
	}

	return true;
}


bool NX_DeinterlacerManager::qSrcBuf(int index, NX_VID_MEMORY_INFO *buf)
{
	NX_PushQueue(&mSrcBufferQueue, (void *)buf);

	return true;
}

bool NX_DeinterlacerManager::dqSrcBuf(int *pIndex, NX_VID_MEMORY_INFO **pBuf)
{
	int dqCount;

	dqCount = (mDeinterMode == DOUBLE_FRAME) ? 2 : 1;

	if (mRunCount >= dqCount) {
		NX_PopQueue(&mSrcBufferQueue, (void **)pBuf);
		mRunCount -= dqCount;

		return true;
	}

	return false;
}

bool NX_DeinterlacerManager::qDstBuf(NX_VID_MEMORY_INFO *buf)
{
	NX_PushQueue(&mDstBufferQueue, (void *)buf);

	return true;
}

bool NX_DeinterlacerManager::dqDstBuf(NX_VID_MEMORY_INFO **pBuf)
{
	NX_PopQueue(&mDstBufferQueue, (void **)pBuf);

	return true;
}

bool NX_DeinterlacerManager::Run(void)
{
	int runCount;

	if (NX_GetQueueCnt(&mSrcBufferQueue) >= 2) {
		if (mHandle < 0) {
			mHandle = open(DEINTERLACER_DEVICE_NODE_NAME, O_RDWR);
			if (mHandle < 0) {
				printf("Fatal Error: can't open device %s\n",
						DEINTERLACER_DEVICE_NODE_NAME);
				return false;
			}
		}

		if (mDeinterMode == DOUBLE_FRAME)
			runCount = NX_GetQueueCnt(&mSrcBufferQueue) * 2 - 2;
		else
			runCount = NX_GetQueueCnt(&mSrcBufferQueue) - 1;

		for (int i = 0; i < runCount; i++) {
			makeFrameInfo(i/2);
			if (ioctl(mHandle, IOCTL_DEINTERLACE_SET_AND_RUN,
						&mFrameInfo) == -1) {
				printf("Critcal Error : set and run failed\n");
				return false;
			}
		}

		mRunCount += runCount;
		return true;
	}
	return false;
}

void NX_DeinterlacerManager::makeFrameInfo(int index)
{
	int Width, Height;
	int srcQueueSize, dstQueueSize;
	NX_DeinterlacerManager::SrcBufferType *srcEntBuf0, *srcEntBuf1;
	NX_VID_MEMORY_INFO *pSrcBuf0, *pSrcBuf1;
	NX_VID_MEMORY_INFO *pDstBuf;

	int srcYStride, srcCStride;
	int dstYStride, dstCStride;

	frame_data_info *pInfo = &mFrameInfo;
	struct frame_data *pSrcFrame0 = &pInfo->src_bufs[0];
	struct frame_data *pSrcFrame1 = &pInfo->src_bufs[1];
	struct frame_data *pSrcFrame2 = &pInfo->src_bufs[2];
	struct frame_data *pDstFrame = &pInfo->dst_bufs[0];

	if (mCropWidth > 0 && mCropHeight > 0) {
		Width = mCropWidth;
		Height = mCropHeight;
	} else {
		Width = mSrcWidth;
		Height = mSrcHeight;
	}

	pInfo->width = Width;
	pInfo->height = Height;
	pInfo->src_type = mSrcType;
	pInfo->src_field = mCurrentField;

	srcQueueSize = NX_GetQueueCnt(&mSrcBufferQueue);
	dstQueueSize = NX_GetQueueCnt(&mDstBufferQueue);

	NX_PeekQueue(&mSrcBufferQueue, srcQueueSize - (index + 1) - 1,
			(void **)&pSrcBuf0);
	NX_PeekQueue(&mSrcBufferQueue, srcQueueSize - index - 1,
			(void **)&pSrcBuf1);

#if _DEBUG
	void *psrc0 = NULL;
	void *psrc1 = NULL;
	char f0_name[200] = {0,};
	char f1_name[200] = {0,};
	static int f0_idx;
	static int f1_idx;
	int w = width, h = r_height;

	int fsize = (w * h) + ((w/2) * (h/2) * 2);
	void *pdst0 = kmalloc(fsize, GFP_KERNEL);
	void *pdst1 = kmalloc(fsize, GFP_KERNEL);

	sprintf(f0_name, "/data/k_src0_%d.yuv", f0_idx++);
	sprintf(f1_name, "/data/k_src1_%d.yuv", f1_idx++);

	struct dma_buf *dma_buf0 = mmap_vaddr_from_fd(src0_y_dma_fd, &psrc0);
	struct dma_buf *dma_buf1 = mmap_vaddr_from_fd(src1_y_dma_fd, &psrc1);

	pr_debug("%s: virt src0 : 0x%p\n", __func__, psrc0);
	pr_debug("%s: virt src1 : 0x%p\n", __func__, psrc1);

	remove_stride(w, h, SRC_ALIGN, pdst0, psrc0);
	write_file(f0_name, pdst0, fsize);

	remove_stride(w, h, SRC_ALIGN, pdst1, psrc1);
	write_file(f1_name, pdst1, fsize);

	munmap_vaddr_from_fd(dma_buf0, psrc0);
	munmap_vaddr_from_fd(dma_buf1, psrc1);

	kfree(pdst0);
	kfree(pdst1);

	pdst0 = NULL;
	pdst1 = NULL;
#endif
	srcYStride = ALIGN(mSrcWidth, SRC_ALIGN);
	srcCStride = ALIGN(mSrcWidth >> 1, SRC_ALIGN >> 1);


	dstYStride = ALIGN(Width, DEINTER_ALIGN);
	dstCStride = ALIGN(Width >> 1, DEINTER_ALIGN >> 1);

	if (mSrcType == SRC_TYPE_PARALLEL) {
		srcYStride *= 2;
		srcCStride *= 2;
	}

	/*	odd - even - odd	*/
	if (mCurrentField == FIELD_EVEN) {
		NX_PeekQueue(&mDstBufferQueue, srcQueueSize - (index + 1) - 1,
				(void **)&pDstBuf);

		/* odd	*/
		pSrcFrame0->frame_num = FIELD_ODD;
		pSrcFrame0->plane_num = pSrcBuf0->planes;
		pSrcFrame0->frame_type = FRAME_SRC;
		pSrcFrame0->plane3.src_stride[0] = srcYStride;
		pSrcFrame0->plane3.src_stride[1] = srcCStride;
		pSrcFrame0->plane3.src_stride[2] = srcCStride;
		pSrcFrame0->plane3.fds[0] = pSrcBuf0->dmaFd[0];
		pSrcFrame0->plane3.fds[1] = pSrcBuf0->dmaFd[1];
		pSrcFrame0->plane3.fds[2] = pSrcBuf0->dmaFd[2];

		/* even	*/
		pSrcFrame1->frame_num = FIELD_EVEN;
		pSrcFrame1->plane_num = pSrcBuf0->planes;
		pSrcFrame1->frame_type = FRAME_SRC;
		pSrcFrame1->plane3.src_stride[0] = srcYStride;
		pSrcFrame1->plane3.src_stride[1] = srcCStride;
		pSrcFrame1->plane3.src_stride[2] = srcCStride;
		pSrcFrame1->plane3.fds[0] = pSrcBuf0->dmaFd[0];
		pSrcFrame1->plane3.fds[1] = pSrcBuf0->dmaFd[1];
		pSrcFrame1->plane3.fds[2] = pSrcBuf0->dmaFd[2];

		/* odd	*/
		pSrcFrame2->frame_num = FIELD_ODD;
		pSrcFrame2->plane_num = pSrcBuf1->planes;
		pSrcFrame2->frame_type = FRAME_SRC;
		pSrcFrame2->plane3.src_stride[0] = srcYStride;
		pSrcFrame2->plane3.src_stride[1] = srcCStride;
		pSrcFrame2->plane3.src_stride[2] = srcCStride;
		pSrcFrame2->plane3.fds[0] = pSrcBuf1->dmaFd[0];
		pSrcFrame2->plane3.fds[1] = pSrcBuf1->dmaFd[1];
		pSrcFrame2->plane3.fds[2] = pSrcBuf1->dmaFd[2];

	} else {
		NX_PeekQueue(&mDstBufferQueue, srcQueueSize - index  - 1,
				(void **)&pDstBuf);

		/* even	*/
		pSrcFrame0->frame_num = FIELD_EVEN;
		pSrcFrame0->plane_num = pSrcBuf0->planes;
		pSrcFrame0->frame_type = FRAME_SRC;
		pSrcFrame0->plane3.src_stride[0] = srcYStride;
		pSrcFrame0->plane3.src_stride[1] = srcCStride;
		pSrcFrame0->plane3.src_stride[2] = srcCStride;
		pSrcFrame0->plane3.fds[0] = pSrcBuf0->dmaFd[0];
		pSrcFrame0->plane3.fds[1] = pSrcBuf0->dmaFd[1];
		pSrcFrame0->plane3.fds[2] = pSrcBuf0->dmaFd[2];

		/* odd	*/
		pSrcFrame1->frame_num = FIELD_ODD;
		pSrcFrame1->plane_num = pSrcBuf1->planes;
		pSrcFrame1->frame_type = FRAME_SRC;
		pSrcFrame1->plane3.src_stride[0] = srcYStride;
		pSrcFrame1->plane3.src_stride[1] = srcCStride;
		pSrcFrame1->plane3.src_stride[2] = srcCStride;
		pSrcFrame1->plane3.fds[0] = pSrcBuf1->dmaFd[0];
		pSrcFrame1->plane3.fds[1] = pSrcBuf1->dmaFd[1];
		pSrcFrame1->plane3.fds[2] = pSrcBuf1->dmaFd[2];

		/* even	*/
		pSrcFrame2->frame_num = FIELD_EVEN;
		pSrcFrame2->plane_num = pSrcBuf1->planes;
		pSrcFrame2->frame_type = FRAME_SRC;
		pSrcFrame2->plane3.src_stride[0] = srcYStride;
		pSrcFrame2->plane3.src_stride[1] = srcCStride;
		pSrcFrame2->plane3.src_stride[2] = srcCStride;
		pSrcFrame2->plane3.fds[0] = pSrcBuf1->dmaFd[0];
		pSrcFrame2->plane3.fds[1] = pSrcBuf1->dmaFd[1];
		pSrcFrame2->plane3.fds[2] = pSrcBuf1->dmaFd[2];

	}

	pDstFrame->frame_num = 0;
	pDstFrame->plane_num = 1;
	pDstFrame->frame_type = FRAME_DST;
	pDstFrame->plane3.dst_stride[0] = dstYStride;
	pDstFrame->plane3.dst_stride[1] = dstCStride;
	pDstFrame->plane3.dst_stride[2] = dstCStride;
	pDstFrame->plane3.fds[0] = pDstBuf->dmaFd[0];
	pDstFrame->plane3.fds[1] = pDstBuf->dmaFd[1];
	pDstFrame->plane3.fds[2] = pDstBuf->dmaFd[2];

	if (mDeinterMode == DOUBLE_FRAME)
		mCurrentField = (mCurrentField == FIELD_EVEN) ?
			FIELD_ODD : FIELD_EVEN;
}
