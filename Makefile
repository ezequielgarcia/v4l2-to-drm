CROSS_COMPILE ?=

CC	:= $(CROSS_COMPILE)gcc
CFLAGS	?= -g -O2 -W -Wall -std=gnu99 `pkg-config --cflags libdrm` -Wno-unused-parameter
LDFLAGS	?= -pthread
LIBS	:= -lrt -ldrm `pkg-config --libs libdrm`

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: test-dmabuf test-mmap test-mmap-vsync test-dry-dmabuf

test-dmabuf: drm.o v4l2.o test-dmabuf.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-mmap: drm.o v4l2.o test-mmap.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-mmap-vsync: drm.o v4l2.o test-mmap-vsync.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

test-dry-dmabuf: drm.o v4l2.o test-dry-dmabuf.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
clean:
	-rm -f *.o test-dmabuf test-mmap test-mmap-vsync test-dry-dmabuf
