CROSS_COMPILE := arm-linux-gnueabi-

CC := $(CROSS_COMPILE)gcc

all: mmap

mmap: mmap.c
	$(CC) -O2 mmap.c -o $@

clean:
	rm -f mmap

