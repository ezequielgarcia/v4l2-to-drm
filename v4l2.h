
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "videodev2.h"

struct buffer {
	void   *start;
	size_t  length;
	int     fence_fd;
	int     dmabuf_fd;
	int     index;
};

extern struct buffer *buffers;

inline static void errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

inline static int xioctl(int fh, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

int v4l2_open(const char *dev_name);
void v4l2_init(int fd, int width, int height);
void v4l2_init_dmabuf(int fd, int *dmabufs, int count);
void v4l2_init_mmap(int fd, int count);
void v4l2_uninit_device(void);
void v4l2_start_capturing_mmap(int fd);
void v4l2_start_capturing_dmabuf(int fd);
void v4l2_stop_capturing(int fd);

void v4l2_dequeue_buffer(int fd, struct v4l2_buffer *buf);
void v4l2_queue_buffer(int fd, int index, int dmabuf_fd);
