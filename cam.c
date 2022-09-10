
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/media.h>

#include "cam.h"
#include "util.h"
#include "conf.h"

static int fd = -1;
static int g_width = 640, g_height = 480;
static int g_buf_count = G_BUF_COUNT;
static int buf_idx = 0;
static buffer_t *buffers = NULL;

// List of V4L2 controls that will be set upon device init
#define NUM_CTRLS 2
struct v4l2_control ctrls[NUM_CTRLS] = {
	{V4L2_CID_HFLIP, 1},
	{V4L2_CID_VFLIP, 1}
};

#ifdef ENABLE_DEBUG
static int check_cnt = 0;
#endif

int sanity_check(int buf_idx, int offset) {
#ifdef ENABLE_DEBUG
	int addr = offset;
	if (check_cnt % 30 == 29) {
		dlog(DLOG_DEBUG "Debug: sanity check: offset = %d\n", offset);
	}
	perror_ret(ioctl(fd, CAM_V2P_IOCTL, &addr), "CAM_V2P_IOCTL");
	assert(buffers[buf_idx].addrPhyY == (void *) addr);
	check_cnt += 1;
#endif
	return 0;
}

int cam_open() {
	fd = open("/dev/video0", O_RDWR, 0);
	perror_ret(fd, "open /dev/video0");
	return 0;
}

// Initialize media bus settings
// This is needed for DVP cameras that uses the V4L2 subdev API
// Probably won't work for USB webcams and such
// For more info, see
// https://www.kernel.org/doc/html/latest/userspace-api/media/mediactl/media-controller.html
//
static int cam_media_init() {
	int ret = 0;
	struct media_v2_entity *mve = NULL;
	struct media_v2_pad *mvp = NULL;

	int mfd = open("/dev/media0", O_RDWR, 0);
	perror_cleanup(mfd, "open /dev/media0");
	int sfd = open("/dev/v4l-subdev0", O_RDWR);
	perror_cleanup(sfd, "open /dev/v4l2-subdev0");

	struct media_device_info mdi;
	CLEAR(mdi);
	perror_cleanup(ioctl(mfd, MEDIA_IOC_DEVICE_INFO, &mdi), "MEDIA_IOC_DEVICE_INFO");

	dlog(DLOG_DEBUG "Debug: media device driver: %s\n", mdi.driver);

	struct media_v2_topology mvt;
	CLEAR(mvt);
	perror_cleanup(ioctl(mfd, MEDIA_IOC_G_TOPOLOGY, &mvt), "MEDIA_IOC_G_TOPOLOGY");

	dlog(DLOG_DEBUG "Debug: %d media entities detected\n", mvt.num_entities);

	mve = calloc(mvt.num_entities, sizeof(*mve));
	mvp = calloc(mvt.num_pads, sizeof(*mvp));
	if (!mve || !mvp) {
		dlog(DLOG_CRIT "Error: mve/mvp calloc() failed\n");
		ret = -1;
		goto cleanup;
	}
	mvt.ptr_entities = (unsigned long) mve;
	mvt.ptr_pads = (unsigned long) mvp;
	perror_cleanup(ioctl(mfd, MEDIA_IOC_G_TOPOLOGY, &mvt), "MEDIA_IOC_G_TOPOLOGY");

	int entity_id = -1, subdev_pad = -1;

	for (int i=0; i<mvt.num_entities; i++) {
		if (strcmp(G_SUBDEV_ENTITY_NAME, mve[i].name) == 0) {
			entity_id = mve[i].id;
			dlog(DLOG_DEBUG "Debug: %s: subdev entity id = %d\n", G_SUBDEV_ENTITY_NAME,
				 entity_id);
		}
	}

	if (entity_id == -1) {
		dlog(DLOG_CRIT "Error: media entity %s not found\n", G_SUBDEV_ENTITY_NAME);
		ret = -1;
		goto cleanup;
	}

	for (int i=0; i<mvt.num_pads; i++) {
		if (mvp[i].entity_id == entity_id) {
			subdev_pad = mvp[i].index;
			dlog(DLOG_DEBUG "Debug: %s: subdev pad = %d\n", G_SUBDEV_ENTITY_NAME, subdev_pad);
		}
	}

	if (subdev_pad == -1) {
		dlog(DLOG_CRIT "Error: no subdev pad found for %s\n", G_SUBDEV_ENTITY_NAME);
		ret = -1;
		goto cleanup;
	}

	struct media_entity_desc dsc = { .id = entity_id };
	perror_cleanup(ioctl(mfd, MEDIA_IOC_ENUM_ENTITIES, &dsc), "MEDIA_IOC_ENUM_ENTITIES");
	dlog(DLOG_DEBUG "Debug: %s: subdev major = %d, minor = %d\n",
		dsc.name, dsc.dev.major, dsc.dev.minor);
	
	// Set frame rate

	struct v4l2_fract fract = { .numerator = 1, .denominator = G_FPS };
	struct v4l2_subdev_frame_interval ival = {
		.interval = fract,
		.pad = subdev_pad,
	};

	perror_cleanup(ioctl(sfd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &ival),
					"VIDIOC_SUBDEV_S_FRAME_INTERVAL");
	
	dlog("Info: %s: frame rate set to %d\n", G_SUBDEV_ENTITY_NAME, G_FPS);

	// Set subdev media bus format

	struct v4l2_subdev_format sfmt = {
		.pad = subdev_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	perror_cleanup(ioctl(sfd, VIDIOC_SUBDEV_G_FMT, &sfmt),
					"VIDIOC_SUBDEV_G_FMT");

	sfmt.format.width = g_width;
	sfmt.format.height = g_height;
	sfmt.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
	sfmt.format.field = V4L2_FIELD_NONE;

	perror_cleanup(ioctl(sfd, VIDIOC_SUBDEV_S_FMT, &sfmt),
					"VIDIOC_SUBDEV_S_FMT");

	dlog("Info: %s: subdev format set to: %dx%d, media bus format code = 0x%x\n",
		G_SUBDEV_ENTITY_NAME, sfmt.format.width, sfmt.format.height, sfmt.format.code);

cleanup:
	if (sfd >= 0) {
		close(sfd);
		sfd = -1;
	}
	if (mve)
		free(mve);
	if (mvp)
		free(mvp);
	if (mfd >= 0) {
		close(mfd);
		mfd = -1;
	}
	return ret;
}

int cam_init(unsigned int width, unsigned int height, unsigned int pixfmt) {

	g_width = width;
	g_height = height;

	if (cam_media_init() < 0) {
		dlog(DLOG_CRIT "Error: cam_media_init() failed\n");
		return -1;
	}

	// Query device capabilities
	struct v4l2_capability cap;
	CLEAR(cap);
	perror_ret(ioctl(fd, VIDIOC_QUERYCAP, &cap), "VIDIOC_QUERYCAP");

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
		!(cap.capabilities & V4L2_CAP_STREAMING)) {
		dlog(DLOG_CRIT "Error: V4L2_CAP_VIDEO_CAPTURE or V4L2_CAP_STREAMING not supported\n");
		return -1;
	}
	
	// Set V4L2 format
	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix.width = width,
		.fmt.pix.height = height,
		.fmt.pix.pixelformat = pixfmt,
	};
	perror_ret(ioctl(fd, VIDIOC_S_FMT, &fmt), "VIDIOC_S_FMT");
	
	// Set V4L2 controls specified in array ctrls[]
	for (int i=0; i<NUM_CTRLS; i++)
		perror_ret(ioctl(fd, VIDIOC_S_CTRL, &ctrls[i]), "VIDIOC_S_CTRL");

	// Request buffers from device (for storing frames later)
	struct v4l2_requestbuffers req = {
		.count = g_buf_count,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
	};
	perror_ret(ioctl(fd, VIDIOC_REQBUFS, &req), "VIDIOC_REQBUFS");

	g_buf_count = req.count;	// Update the actual number of buffers expected by device

	buffers = calloc(g_buf_count, sizeof(*buffers));

	if (buffers == NULL) {
		dlog(DLOG_CRIT "Error: buffer calloc() failed\n");
		return -1;
	}

	// MMAP buffers
	for (int i=0; i<g_buf_count; i++) {
		struct v4l2_buffer buf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};
		perror_ret(ioctl(fd, VIDIOC_QUERYBUF, &buf), "VIDIOC_QUERYBUF");

		buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
								MAP_SHARED, fd, buf.m.offset);
		buffers[i].length = buf.length;
		buffers[i].addrVirY = buffers[i].start;
	
		// This ALIGN_16B thing is wrong?
		// At 1920x1080, this will screw up the data alignment and create a green band in video

		//buffers[i].addrVirC = buffers[i].start + ALIGN_16B(g_width) * ALIGN_16B(g_height);
		buffers[i].addrVirC = buffers[i].start + g_width * g_height;
		
		int addr = buf.m.offset;

		// dirty hack to get physical address of buffers
		// see github repo README for details
		perror_ret(ioctl(fd, CAM_V2P_IOCTL, &addr), "CAM_V2P_IOCTL");

		buffers[i].addrPhyY = (void *) addr;
		//buffers[i].addrPhyC = addr + ALIGN_16B(g_width) * ALIGN_16B(g_height);
		buffers[i].addrPhyC = (void *) (addr + g_width * g_height);
	}
	return 0;
}

int cam_start_capture() {
	for (int i=0; i<g_buf_count; i++) {
		struct v4l2_buffer buf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};
		perror_ret(ioctl(fd, VIDIOC_QBUF, &buf), "VIDIOC_QBUF");
		sanity_check(i, buf.m.offset);
	}
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	perror_ret(ioctl(fd, VIDIOC_STREAMON, &type), "VIDIOC_STREAMON");
	return 0;
}

// Returns non-negative dequeued buffer index upon success
int cam_dqbuf() {
	struct v4l2_buffer buf = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
		.index = buf_idx,
	};
	perror_ret(ioctl(fd, VIDIOC_DQBUF, &buf), "VIDIOC_DQBUF");
	sanity_check(buf_idx, buf.m.offset);
	return buf_idx;
}

buffer_t *cam_get_buf(int idx) {
	if (0 <= idx && idx < g_buf_count)
		return buffers + idx;
	return NULL;
}

// Enqueue the last dequeued buffer back to the camera device
int cam_qbuf() {
	struct v4l2_buffer buf = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
		.index = buf_idx,
	};
	perror_ret(ioctl(fd, VIDIOC_QBUF, &buf), "VIDIOC_QBUF");
	sanity_check(buf_idx, buf.m.offset);
	buf_idx = (buf_idx + 1) % g_buf_count;
	return 0;
}

int cam_stop_capture() {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	perror_ret(ioctl(fd, VIDIOC_STREAMOFF, &type), "VIDIOC_STREAMOFF");
	return 0;
}

void cam_deinit() {
	if (buffers) {
		for (int i=0; i<g_buf_count; i++)
			munmap(buffers[i].start, buffers[i].length);
		free(buffers);
		buffers = NULL;
	}
}

void cam_close() {
	if (fd != -1) {
		close(fd);
		fd = -1;
	}
}

