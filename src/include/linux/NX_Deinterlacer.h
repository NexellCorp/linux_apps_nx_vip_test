#ifndef __NX_DEINTERLACE_H__
#define __NX_DEINTERLACE_H__

#include "ioc_magic.h"

#define MAX_BUFFER_PLANES	3
#define SRC_BUFFER_COUNT  3
#define DST_BUFFER_COUNT  1

enum nx_deinter_src_type {
        SRC_TYPE_MIPI = 0ul,
        SRC_TYPE_PARALLEL
};

enum nx_deinter_src_field {
        FIELD_EVEN = 0ul,
        FIELD_ODD = 1ul
};

struct frame_data {
	int frame_num;
	int plane_num;
	int frame_type;
	int frame_factor;

	union {
		struct {
			unsigned char *virt[MAX_BUFFER_PLANES];
			unsigned long sizes[MAX_BUFFER_PLANES];
			unsigned long src_stride[MAX_BUFFER_PLANES];
			unsigned long dst_stride[MAX_BUFFER_PLANES];

			int fds[MAX_BUFFER_PLANES];
			unsigned long phys[MAX_BUFFER_PLANES];
		} plane3;

		struct {
			unsigned char *virt[MAX_BUFFER_PLANES-1];
			unsigned long sizes[MAX_BUFFER_PLANES-1];
			unsigned long src_stride[MAX_BUFFER_PLANES-1];
			unsigned long dst_stride[MAX_BUFFER_PLANES-1];

			int fds[MAX_BUFFER_PLANES-1];
			unsigned long phys[MAX_BUFFER_PLANES-1];
		} plane2;
	};
};

typedef struct {
	int command;
	int width;
	int height;
	int plane_mode;
	enum nx_deinter_src_type src_type;
	enum nx_deinter_src_field src_field;

	struct frame_data dst_bufs[DST_BUFFER_COUNT];
	struct frame_data src_bufs[SRC_BUFFER_COUNT];
} frame_data_info;

enum {
	IOCTL_DEINTERLACE_SET_AND_RUN	= _IO(IOC_NX_MAGIC, 1),
};
#endif
