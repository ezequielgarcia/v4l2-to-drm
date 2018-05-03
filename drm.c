#define _GNU_SOURCE
#define _XOPEN_SOURCE 701

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libdrm/drm.h>
#include "drm.h"

struct color_rgb32 {
	uint32_t value;
};

struct util_color_component {
        unsigned int length;
        unsigned int offset;
};

struct util_rgb_info {
        struct util_color_component red;
        struct util_color_component green;
        struct util_color_component blue;
        struct util_color_component alpha;
};

#define MAKE_RGB_INFO(rl, ro, gl, go, bl, bo, al, ao) \
        { { (rl), (ro) }, { (gl), (go) }, { (bl), (bo) }, { (al), (ao) } }

#define MAKE_RGBA(rgb, r, g, b, a) \
        ((((r) >> (8 - (rgb)->red.length)) << (rgb)->red.offset) | \
         (((g) >> (8 - (rgb)->green.length)) << (rgb)->green.offset) | \
         (((b) >> (8 - (rgb)->blue.length)) << (rgb)->blue.offset) | \
         (((a) >> (8 - (rgb)->alpha.length)) << (rgb)->alpha.offset))

#define MAKE_RGB24(rgb, r, g, b) \
        { .value = MAKE_RGBA(rgb, r, g, b, 0) }

enum {
	DEPTH = 24,
	BPP = 32,
};

static int eopen(const char *path, int flag)
{
	int fd;

	if ((fd = open(path, flag)) < 0) {
		fprintf(stderr, "cannot open \"%s\"\n", path);
		error("open");
	}
	return fd;
}

static void *emmap(int addr, size_t len, int prot, int flag, int fd, off_t offset)
{
	uint32_t *fp;

	if ((fp = (uint32_t *) mmap(0, len, prot, flag, fd, offset)) == MAP_FAILED)
		error("mmap");
	return fp;
}

int drm_open(const char *path)
{
	int fd, flags;
	uint64_t has_dumb;

	fd = eopen(path, O_RDWR);

	/* set FD_CLOEXEC flag */
	if ((flags = fcntl(fd, F_GETFD)) < 0
		|| fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
		fatal("fcntl FD_CLOEXEC failed");

	/* check capability */
	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || has_dumb == 0)
		fatal("drmGetCap DRM_CAP_DUMB_BUFFER failed or doesn't have dumb buffer");

	return fd;
}

struct drm_dev_t *drm_find_dev(int fd)
{
	int i, m;
	struct drm_dev_t *dev = NULL, *dev_head = NULL;
	drmModeRes *res;
	drmModeConnector *conn;
	drmModeEncoder *enc;
	drmModeModeInfo *mode = NULL, *preferred = NULL;

	if ((res = drmModeGetResources(fd)) == NULL)
		fatal("drmModeGetResources() failed");

	/* find all available connectors */
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);

		if (conn != NULL && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
			dev = (struct drm_dev_t *) malloc(sizeof(struct drm_dev_t));
			memset(dev, 0, sizeof(struct drm_dev_t));

			/* find preferred mode */
			for (m = 0; m < conn->count_modes; m++) {
				mode = &conn->modes[m];
				if (mode->type & DRM_MODE_TYPE_PREFERRED)
					preferred = mode;
				fprintf(stdout, "mode: %dx%d %s\n", mode->hdisplay, mode->vdisplay, mode->type & DRM_MODE_TYPE_PREFERRED ? "*" : "");
			}

			if (!preferred)
				preferred = &conn->modes[0];

			dev->conn_id = conn->connector_id;
			dev->enc_id = conn->encoder_id;
			dev->next = NULL;

			memcpy(&dev->mode, preferred, sizeof(drmModeModeInfo));
			dev->width = preferred->hdisplay;
			dev->height = preferred->vdisplay;

			/* FIXME: use default encoder/crtc pair */
			if ((enc = drmModeGetEncoder(fd, dev->enc_id)) == NULL)
				fatal("drmModeGetEncoder() faild");
			dev->crtc_id = enc->crtc_id;
			drmModeFreeEncoder(enc);

			dev->saved_crtc = NULL;

			/* create dev list */
			dev->next = dev_head;
			dev_head = dev;
		}
		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);

	printf("selected connector(s)\n");
	for (dev = dev_head; dev != NULL; dev = dev->next) {
		printf("connector id:%d\n", dev->conn_id);
		printf("\tencoder id:%d crtc id:%d\n", dev->enc_id, dev->crtc_id);
		printf("\twidth:%d height:%d\n", dev->width, dev->height);
	}

	return dev_head;
}

void drm_setup_fb(int fd, struct drm_dev_t *dev, int map, int export)
{
	int i;

	for (i = 0; i < BUFCOUNT; i++) {
		struct drm_mode_create_dumb create_req;
		struct drm_mode_map_dumb map_req;

		memset(&create_req, 0, sizeof(struct drm_mode_create_dumb));
		create_req.width = dev->width;
		create_req.height = dev->height;
		create_req.bpp = BPP; // hard conding

		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req) < 0)
			fatal("drmIoctl DRM_IOCTL_MODE_CREATE_DUMB failed");

		dev->bufs[i].pitch = create_req.pitch;
		dev->bufs[i].size = create_req.size;
		/* GEM buffer handle */
		dev->bufs[i].bo_handle = create_req.handle;

		if (drmModeAddFB(fd, dev->width, dev->height,
			DEPTH, BPP, dev->bufs[i].pitch, dev->bufs[i].bo_handle, &dev->bufs[i].fb_id))
			fatal("drmModeAddFB failed");

		if (export) {
			int ret; 

			ret = drmPrimeHandleToFD(fd, dev->bufs[i].bo_handle,
				DRM_CLOEXEC | DRM_RDWR, &dev->bufs[i].dmabuf_fd);
			if (ret < 0)
				fatal("could not export the dump buffer");
		}

		if (map) {
			memset(&map_req, 0, sizeof(struct drm_mode_map_dumb));
			map_req.handle = dev->bufs[i].bo_handle;

			if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req))
				fatal("drmIoctl DRM_IOCTL_MODE_MAP_DUMB failed");
			dev->bufs[i].buf = (uint32_t *) emmap(0, dev->bufs[i].size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_req.offset);
		}
	}

	dev->pitch = dev->bufs[0].pitch;
	dev->saved_crtc = drmModeGetCrtc(fd, dev->crtc_id); /* must store crtc data */

	/* Stop before screwing up the monitor */
	getchar();

	/* First buffer to DRM */
	if (drmModeSetCrtc(fd, dev->crtc_id, dev->bufs[0].fb_id, 0, 0, &dev->conn_id, 1, &dev->mode))
		fatal("drmModeSetCrtc() failed");

	/* First flip */
	drmModePageFlip(fd, dev->crtc_id,
                        dev->bufs[0].fb_id, DRM_MODE_PAGE_FLIP_EVENT,
                        dev);
}

void drm_destroy(int fd, struct drm_dev_t *dev_head)
{
	struct drm_dev_t *devp, *devp_tmp;
	struct drm_mode_destroy_dumb dreq;
	int i;

	for (devp = dev_head; devp != NULL;) {
		if (devp->saved_crtc)
			drmModeSetCrtc(fd, devp->saved_crtc->crtc_id, devp->saved_crtc->buffer_id,
				devp->saved_crtc->x, devp->saved_crtc->y, &devp->conn_id, 1, &devp->saved_crtc->mode);
		drmModeFreeCrtc(devp->saved_crtc);

		for (i = 0; i < BUFCOUNT; i++) {
			munmap(devp->bufs[i].buf, devp->bufs[i].size);

			drmModeRmFB(fd, devp->bufs[i].fb_id);

			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = devp->bufs[i].bo_handle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

			/* TODO: Prime release */
		}

		devp_tmp = devp;
		devp = devp->next;
		free(devp_tmp);
	}

	close(fd);
}

void fill_smpte_rgb32(void *mem,
	unsigned int width, unsigned int height)
{
	struct util_rgb_info _rgb = MAKE_RGB_INFO(8, 16, 8, 8, 8, 0, 0, 0);
	struct util_rgb_info *rgb = &_rgb;
	int stride = width * BPP / 8;
	
	const struct color_rgb32 colors_top[] = {
		MAKE_RGB24(rgb, 192, 192, 192),	/* grey */
		MAKE_RGB24(rgb, 192, 192, 0),	/* yellow */
		MAKE_RGB24(rgb, 0, 192, 192),	/* cyan */
		MAKE_RGB24(rgb, 0, 192, 0),	/* green */
		MAKE_RGB24(rgb, 192, 0, 192),	/* magenta */
		MAKE_RGB24(rgb, 192, 0, 0),	/* red */
		MAKE_RGB24(rgb, 0, 0, 192),	/* blue */
	};
	const struct color_rgb32 colors_middle[] = {
		MAKE_RGB24(rgb, 0, 0, 192),	/* blue */
		MAKE_RGB24(rgb, 19, 19, 19),	/* black */
		MAKE_RGB24(rgb, 192, 0, 192),	/* magenta */
		MAKE_RGB24(rgb, 19, 19, 19),	/* black */
		MAKE_RGB24(rgb, 0, 192, 192),	/* cyan */
		MAKE_RGB24(rgb, 19, 19, 19),	/* black */
		MAKE_RGB24(rgb, 192, 192, 192),	/* grey */
	};
	const struct color_rgb32 colors_bottom[] = {
		MAKE_RGB24(rgb, 0, 33, 76),	/* in-phase */
		MAKE_RGB24(rgb, 255, 255, 255),	/* super white */
		MAKE_RGB24(rgb, 50, 0, 106),	/* quadrature */
		MAKE_RGB24(rgb, 19, 19, 19),	/* black */
		MAKE_RGB24(rgb, 9, 9, 9),	/* 3.5% */
		MAKE_RGB24(rgb, 19, 19, 19),	/* 7.5% */
		MAKE_RGB24(rgb, 29, 29, 29),	/* 11.5% */
		MAKE_RGB24(rgb, 19, 19, 19),	/* black */
	};
	unsigned int x;
	unsigned int y;

	for (y = 0; y < height * 6 / 9; ++y) {
		for (x = 0; x < width; ++x)
			((struct color_rgb32 *)mem)[x] =
				colors_top[x * 7 / width];
		mem += stride;
	}

	for (; y < height * 7 / 9; ++y) {
		for (x = 0; x < width; ++x)
			((struct color_rgb32 *)mem)[x] =
				colors_middle[x * 7 / width];
		mem += stride;
	}

	for (; y < height; ++y) {
		for (x = 0; x < width * 5 / 7; ++x)
			((struct color_rgb32 *)mem)[x] =
				colors_bottom[x * 4 / (width * 5 / 7)];
		for (; x < width * 6 / 7; ++x)
			((struct color_rgb32 *)mem)[x] =
				colors_bottom[(x - width * 5 / 7) * 3
					      / (width / 7) + 4];
		for (; x < width; ++x)
			((struct color_rgb32 *)mem)[x] = colors_bottom[7];
		mem += stride;
	}
}
