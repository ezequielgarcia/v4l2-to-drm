CROSS_COMPILE ?=

CC	:= $(CROSS_COMPILE)gcc
CFLAGS	?= -g -O2 -W -Wall -std=gnu99 `pkg-config --cflags libdrm`
LDFLAGS	?= -pthread
LIBS	:= -lrt -ldrm `pkg-config --libs libdrm`

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: test

test: drm.o v4l2.o main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
clean:
	-rm -f *.o test
