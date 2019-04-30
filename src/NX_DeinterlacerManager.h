#ifndef __NX_DEINNX_VID_MEMORY_INFOERLACER_MANANER__
#define __NX_DEINNX_VID_MEMORY_INFOERLACER_MANAGER__

#include <NX_Queue.h>
#include "NX_Deinterlacer.h"
#include "NX_CV4l2Camera.h"

#define DEINTER_BUF_NUM	8

struct NX_DEINTER_PARAM {
	int srcWidth;
	int srcHeight;
	int cropWidth;
	int cropHeight;
	int format;
	int planes;
	int bufNum;
};

struct NX_DEINTER_INFO {
	int32_t width;
	int32_t height;
	int32_t planes;
	int32_t pixelFormat;

	int32_t drmFd;
	int32_t gemFds[DEINTER_BUF_NUM];
	int32_t dmaFds[DEINTER_BUF_NUM];
	int32_t deinterBufNum;
	int32_t deinterBufSize;
	void*	pVaddr[DEINTER_BUF_NUM];
};

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

class NX_DeinterlacerManager
{
public:
	NX_DeinterlacerManager(void);
	NX_DeinterlacerManager(struct NX_DEINTER_PARAM param);
	virtual ~NX_DeinterlacerManager();

	virtual int DeinterCreateBuffer(void);
	virtual int DeinterReleaseBuffer(void);
	virtual bool GetBuffer(NX_VID_MEMORY_INFO *memInfo, int index);
	virtual bool qSrcBuf(int index, NX_VID_MEMORY_INFO *buf);
	virtual bool dqSrcBuf(int *pIndex, NX_VID_MEMORY_INFO **buf);
	virtual bool qDstBuf(NX_VID_MEMORY_INFO *buf);
	virtual bool dqDstBuf(NX_VID_MEMORY_INFO **pBuf);
	virtual bool Run(void);
	virtual int getRunCount(void) { return mRunCount; }
	virtual void setSrcType(enum nx_deinter_src_type type)
	{
		mSrcType = type;
	}
	virtual void setStartFild(enum nx_deinter_src_field field)
	{
		mCurrentField = field;
	}

	enum NX_DEINTERLACER_MODE {
		SINGLE_FRAME = 0ul,
		DOUBLE_FRAME
	};

	enum {
		FRAME_SRC = 1ul,
		FRAME_DST
	};

private:
	struct SrcBufferType {
		int index;
		NX_VID_MEMORY_INFO *buf;
	};

	int mSrcWidth;
	int mSrcHeight;
	int mCropWidth;
	int mCropHeight;
	int mPixelFormat;
	int mPlanes;

	NX_QUEUE mSrcBufferQueue;
	NX_QUEUE mDstBufferQueue;

	int mHandle;
	int mRunCount;
	int mDrmHandle;

	frame_data_info mFrameInfo;

	int mBufNum;

	enum nx_deinter_src_type mSrcType;
	enum nx_deinter_src_field mCurrentField;
	enum NX_DEINTERLACER_MODE mDeinterMode;

	struct NX_DEINTER_INFO mDeinterInfo;

private:
	int CalcAllocSize(int width, int height, int format);
	void makeFrameInfo(int index);
};
#endif
