
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "videodev2.h"
#include "drm.h"
#include "v4l2.h"

static const char *dri_path = "/dev/dri/card0";
static const char *v4l2_path = "/dev/video0";

static void mainloop(int v4l2_fd, int drm_fd, struct drm_dev_t *dev)
{
	struct v4l2_buffer buf;
	int r;

	struct pollfd fds[] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = v4l2_fd, .events = POLLIN },
	};

	while (1) {
		r = poll(fds, 2, 3000);
		if (-1 == r) {
			if (EINTR == errno)
				continue;
			errno_print("select");
		}

		if (0 == r) {
			fprintf(stderr, "timeout\n");
			exit(EXIT_FAILURE);
		}

		if (fds[0].revents & POLLIN) {
			fprintf(stdout, "User requested exit\n");
			return;
		}
		if (fds[1].revents & POLLIN) {
			/* We can see how DQBUF/QBUF operations
			 * act as the implicit synchronization
			 * mechanism here.
			 */
			v4l2_dequeue_buffer(v4l2_fd, &buf);
			memcpy(dev->bufs[0].buf, buffers[buf.index].start, buf.bytesused);
			v4l2_queue_buffer(v4l2_fd, buf.index, -1);
	
			drmModePageFlip(drm_fd, dev->crtc_id, dev->bufs[0].fb_id,
				      DRM_MODE_PAGE_FLIP_EVENT, dev);
		}
	}
}

int main()
{
	struct drm_dev_t *dev_head, *dev;
	int v4l2_fd, drm_fd;

	drm_fd = drm_open(dri_path);
	dev_head = drm_find_dev(drm_fd);

	if (dev_head == NULL) {
		fprintf(stderr, "available drm_dev not found\n");
		return EXIT_FAILURE;
	}

	dev = dev_head;
	drm_setup_fb(drm_fd, dev, 1, 0);

	v4l2_fd = v4l2_open(v4l2_path);
	v4l2_init(v4l2_fd, dev->width, dev->height, dev->pitch);
	v4l2_init_mmap(v4l2_fd, BUFCOUNT);
	v4l2_start_capturing_mmap(v4l2_fd);

	dev->v4l2_fd = v4l2_fd;
	dev->drm_fd = drm_fd;

	mainloop(v4l2_fd, drm_fd, dev);

	drm_destroy(drm_fd, dev_head);
	return 0;
}
