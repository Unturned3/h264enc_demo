
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <linux/media.h>

#include "h264.h"
#include "cam.h"
#include "util.h"
#include "conf.h"

/*
	More detail on the v4l2 / media-ctl API:
	https://docs.kernel.org/userspace-api/media/v4l/v4l2.html
	https://www.kernel.org/doc/html/latest/userspace-api/media/mediactl/media-controller.html

	Getting subdevice name from major & minor numbers:
	https://git.linuxtv.org/v4l-utils.git/tree/utils/media-ctl/libmediactl.c
	in function 'media_get_devname_sysfs()'
*/

void usage(char *argv0) {
	dlog(DLOG_WARN
		"Usage: %s [width] [height] [FPS] [n_frames]\n"
		"Supported formats: 640x480, 1280x720, 1920x1080\n"
		"All formats support 30FPS; 640x480 also supports 60FPS.\n"
		"n_frames: number of frames to capture; defaults to 450 if omitted.\n"
		, argv0);
}

int main(int argc, char **argv) {
#ifdef ENABLE_DEBUG
	dlog_set_level(DLOG_DEBUG);
#endif
	int ret = 0;

	if (argc < 4 || argc > 5) {
		usage(argv[0]);
		return 0;
	}

	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	int fps = atoi(argv[3]);
	int n_frames = G_FRAMES;
	if (argc == 5) {
		n_frames = atoi(argv[4]);
		dlog_cleanup(n_frames, DLOG_CRIT "Error: n_frames must be non-negative\n");
	}

	if ((width == 640 && height == 480) ||
		(width == 1280 && height == 720) ||
		(width == 1920 && height == 1080)) {

		dlog_cleanup(h264_init(width, height, fps), DLOG_CRIT "Error: h264_init() failed\n");
		dlog_cleanup(cam_open(), DLOG_CRIT "Error: cam_open() failed\n");
		dlog_cleanup(cam_init(width, height, G_V4L2_PIX_FMT, fps),
					DLOG_CRIT "Error: cam_init() failed\n");
	}
	else {
		dlog(DLOG_CRIT "Error: unsupported width/height\n");
		usage(argv[0]);
		return 0;
	}

	dlog_cleanup(cam_start_capture(), DLOG_CRIT "Error: cam_start_capture() failed\n");

	rt_timer_start();

	// Capture frames
	for (int i=0; i<n_frames; i++) {

		// Dequeue buffer and get its index
		int j = cam_dqbuf();
		dlog_cleanup(j, DLOG_CRIT "Error: cam_dqbuf() failed");
		buffer_t *buf = cam_get_buf(j);

		// Encode frame
		dlog_cleanup(h264_encode(buf->addrPhyY, buf->addrPhyC), DLOG_CRIT "Error: h264_encode() failed\n");

		if (ENABLE_SAVE) {
			char output_file[32] = "frame";
			char suffix_str[32];
			sprintf(suffix_str, "%02d", i);
			strcat(output_file, suffix_str);
			FILE *fp = fopen(output_file, "wb");
			fwrite(buf->start, 1, buf->length, fp);
			fclose(fp);
		}

		// Queue the recently dequeued buffer back to the device
		dlog_cleanup(cam_qbuf(), DLOG_CRIT "Error: cam_qbuf() failed\n");
	}

	rt_timer_stop();
	double elapsed = rt_timer_elapsed();

	dlog("\nInfo: captured %d frames in %.2fs; FPS = %.1f\n",
		n_frames, elapsed, n_frames / elapsed);

cleanup:
	cam_stop_capture();
	cam_deinit();
	cam_close();
	h264_deinit();
	return ret;
}

