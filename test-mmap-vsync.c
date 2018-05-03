
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "videodev2.h"
#include "drm.h"
#include "v4l2.h"

static const char *dri_path = "/dev/dri/card0";
static const char *v4l2_path = "/dev/video0";
static int next_buffer_index = -1;
static int curr_buffer_index = 0;

static void page_flip_handler(int fd, unsigned int frame,
			unsigned int sec, unsigned int usec,
			void *data)
{
	struct drm_dev_t *dev = data;

	/* If we have a next buffer, then let's return the current one,
	 * and grab the next one.
	 */
	if (next_buffer_index > 0) {
		v4l2_queue_buffer(dev->v4l2_fd, curr_buffer_index, -1);
		curr_buffer_index = next_buffer_index;
		next_buffer_index = -1;
	}
	drmModePageFlip(fd, dev->crtc_id, dev->bufs[curr_buffer_index].fb_id,
			      DRM_MODE_PAGE_FLIP_EVENT, dev);
}


static void mainloop(int v4l2_fd, int drm_fd, struct drm_dev_t *dev)
{
	struct v4l2_buffer buf;
	drmEventContext ev;
	int r;

        memset(&ev, 0, sizeof ev);
        ev.version = DRM_EVENT_CONTEXT_VERSION;
        ev.vblank_handler = NULL;
        ev.page_flip_handler = page_flip_handler;

	struct pollfd fds[] = {
		{ .fd = STDIN_FILENO, .events = POLLIN },
		{ .fd = v4l2_fd, .events = POLLIN },
		{ .fd = drm_fd, .events = POLLIN },
	};

	while (1) {
		r = poll(fds, 3, 3000);
		if (-1 == r) {
			if (EINTR == errno)
				continue;
			errno_print("poll");
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
			/* Video buffer captured, dequeue it
			 * and store it for scanout.
			 */
			int dequeued = v4l2_dequeue_buffer(v4l2_fd, &buf);
			if (dequeued) {
				/* Copy to scanout buffer */
				memcpy(dev->bufs[buf.index].buf, buffers[buf.index].start, buf.bytesused);
				/* Set next buffer */
				next_buffer_index = buf.index;
			}
		}
		if (fds[2].revents & POLLIN) {
			drmHandleEvent(drm_fd, &ev);
		}
	}
}

int main()
{
	struct drm_dev_t *dev_head, *dev;
	int v4l2_fd, drm_fd;

	drm_fd = drm_open(dri_path, 1, 0);
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
