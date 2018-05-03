
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

static int wait_poll(int fd)
{
	struct pollfd fds[1];

        fds[0].fd = fd;
        fds[0].events = POLLIN | POLLERR;
        return poll(fds, 1, 2000);
}

static void mainloop(int v4l2_fd, int drm_fd, struct drm_dev_t *dev)
{
	struct v4l2_buffer buf;
	unsigned int count;
	int r;

	count = frame_count;

	while (count-- > 0) {
		for (;;) {
			r = wait_poll(v4l2_fd);
			if (-1 == r) {
				if (EINTR == errno)
					continue;
				errno_exit("select");
			}

			if (0 == r) {
				fprintf(stderr, "timeout\n");
				exit(EXIT_FAILURE);
			}

			/* We can see how DQBUF/QBUF operations
			 * act as the implicit synchronization
			 * mechanism here.
			 */
			v4l2_dequeue_buffer(v4l2_fd, &buf);
			memcpy(dev->bufs[0].buf, buffers[buf.index].start, buf.bytesused);
			v4l2_queue_buffer(v4l2_fd, buf.index, -1);

			drmModeDirtyFB(drm_fd, dev->bufs[0].fb_id, NULL, 0);
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

	getchar();

	dev = dev_head;
	drm_setup_fb(drm_fd, dev, 1, 0);

	v4l2_fd = v4l2_open(v4l2_path);
	v4l2_init(v4l2_fd, dev->width, dev->height);
	v4l2_init_mmap(v4l2_fd, BUFCOUNT);
	v4l2_start_capturing_mmap(v4l2_fd);

	mainloop(v4l2_fd, drm_fd, dev);

	drm_destroy(drm_fd, dev_head);
	return 0;
}
