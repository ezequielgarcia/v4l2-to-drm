
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "videodev2.h"
#include "drm.h"
#include "v4l2.h"

static const char *dri_path = "/dev/dri/card1";
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
			printf("error in poll %d", errno);
			return;
		}

		if (0 == r) {
			fprintf(stderr, "timeout\n");
			return;
		}

		if (fds[0].revents & POLLIN) {
			fprintf(stdout, "User requested exit\n");
			return;
		}
		if (fds[1].revents & POLLIN) {
			/* A dummy re-queue */
			int dequeued = v4l2_dequeue_buffer(v4l2_fd, &buf);
			if (dequeued)
				v4l2_queue_buffer(v4l2_fd, buf.index, dev->bufs[buf.index].dmabuf_fd);
			fflush(stderr);
			fprintf(stderr, ".");
			fflush(stdout);
		}
	}
}

int main()
{
	struct drm_dev_t *dev;
	int v4l2_fd, drm_fd;
	int dmabufs[BUFCOUNT];

	drm_fd = drm_open(dri_path, 1, 1);
	dev = (struct drm_dev_t *) malloc(sizeof(struct drm_dev_t));
	memset(dev, 0, sizeof(struct drm_dev_t));

	/* Exported, not mapped, no framebuffer */
	dev->width = 1280;
	dev->height = 720;
	drm_setup_dummy(drm_fd, dev, 0, 1);

	dmabufs[0] = dev->bufs[0].dmabuf_fd;
	dmabufs[1] = dev->bufs[1].dmabuf_fd;
	dmabufs[2] = dev->bufs[2].dmabuf_fd;
	dmabufs[3] = dev->bufs[3].dmabuf_fd;

	v4l2_fd = v4l2_open(v4l2_path);
	v4l2_init(v4l2_fd, dev->width, dev->height, dev->pitch);
	v4l2_init_dmabuf(v4l2_fd, dmabufs, BUFCOUNT);
	v4l2_start_capturing_dmabuf(v4l2_fd);

	dev->v4l2_fd = v4l2_fd;
	dev->drm_fd = drm_fd;

	mainloop(v4l2_fd, drm_fd, dev);

	drm_destroy(drm_fd, dev);
	return 0;
}
