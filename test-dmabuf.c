
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "videodev2.h"
#include "drm.h"
#include "v4l2.h"

static const char *dri_path = "/dev/dri/card0";
static const char *v4l2_path = "/dev/video0";
static int frame_count = -1;

static int wait_poll(int v4l2_fd, int drm_fd)
{
	struct pollfd fds[] = {
		{ .fd = v4l2_fd, .events = POLLIN },
		{ .fd = drm_fd, .events = POLLIN },
	};

        return poll(fds, 2, 2000);
}

static void mainloop(int v4l2_fd, int drm_fd, struct drm_dev_t *dev)
{
	struct v4l2_buffer buf;
	unsigned int count, current;
	int r;

	count = frame_count;
	current = 0;

	while (count-- > 0) {
		for (;;) {
			r = wait_poll(v4l2_fd, drm_fd);
			if (-1 == r) {
				if (EINTR == errno)
					continue;
				errno_exit("select");
			}

			if (0 == r) {
				fprintf(stderr, "timeout\n");
				exit(EXIT_FAILURE);
			}

			v4l2_dequeue_buffer(v4l2_fd, &buf);

			/* kind of a buffer flip, but wrong */
			drmModeSetCrtc(drm_fd, dev->crtc_id, dev->bufs[buf.index].fb_id,
				       0, 0, &dev->conn_id, 1, &dev->mode);
			drmModeDirtyFB(drm_fd, dev->bufs[buf.index].fb_id, NULL, 0);

			v4l2_queue_buffer(v4l2_fd, current, dev->bufs[current].dmabuf_fd);
			current = buf.index;
		}
	}
}

int main()
{
	struct drm_dev_t *dev_head, *dev;
	int v4l2_fd, drm_fd;
	int dmabufs[BUFCOUNT];

	drm_fd = drm_open(dri_path);
	dev_head = drm_find_dev(drm_fd);

	if (dev_head == NULL) {
		fprintf(stderr, "available drm_dev not found\n");
		return EXIT_FAILURE;
	}

	getchar();

	dev = dev_head;
	drm_setup_fb(drm_fd, dev, 0, 1);

	dmabufs[0] = dev->bufs[0].dmabuf_fd;
	dmabufs[1] = dev->bufs[1].dmabuf_fd;
	dmabufs[2] = dev->bufs[2].dmabuf_fd;
	dmabufs[3] = dev->bufs[3].dmabuf_fd;

	v4l2_fd = v4l2_open(v4l2_path);
	v4l2_init(v4l2_fd, dev->width, dev->height);
	v4l2_init_dmabuf(v4l2_fd, dmabufs, BUFCOUNT);
	v4l2_start_capturing_dmabuf(v4l2_fd);

	mainloop(v4l2_fd, drm_fd, dev);

	drm_destroy(drm_fd, dev_head);
	return 0;
}
