
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct drm_dev_t {
	uint32_t *buf;
	uint32_t conn_id, enc_id, crtc_id, fb_id;
	uint32_t width, height;
	uint32_t pitch, size, handle;
	drmModeModeInfo mode;
	drmModeCrtc *saved_crtc;
	struct drm_dev_t *next;
};

inline static void fatal(char *str)
{
	fprintf(stderr, "%s\n", str);
	exit(EXIT_FAILURE);
}

inline static void error(char *str)
{
	perror(str);
	exit(EXIT_FAILURE);
}

int drm_open(const char *path);
struct drm_dev_t *drm_find_dev(int fd);
void drm_setup_fb(int fd, struct drm_dev_t *dev);
void drm_destroy(int fd, struct drm_dev_t *dev_head);
