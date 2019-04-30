AM_CPPFLAGS = \
	$(WARN_CFLAGS)	\
	-I./include/linux \
	-I${includedir} \
	-I${includedir}/libdrm

#vip_test_LDFLAGS = --static

vip_test_LDADD = \
	-L${libdir} \
	-ldrm \
	-lnx_video_api \
	-lnx_drm_allocator \
	-lnx_v4l2 \
	-lpthread

vip_test_SOURCES = \
	DrmRender.cpp \
	NX_CV4l2Camera.cpp \
	NX_Queue.cpp \
	Util.cpp \
	NX_V4l2Utils.cpp \
	CameraTest.cpp \
	NX_DeinterlacerManager.cpp \
	main.cpp

bin_PROGRAMS = vip_test
