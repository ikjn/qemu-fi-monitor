#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/mman.h>

#define ERROR(fmt, ...) do { fprintf (stderr, fmt, ##__VA_ARGS__); exit(-1); } while (0)
#define WARNING(fmt, ...) do { fprintf (stderr, fmt, ##__VA_ARGS__); } while (0)
#define INFO(fmt, ...) do { fprintf (stdout, fmt, ##__VA_ARGS__); } while (0)

#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#define PAGE_MASK   (~(PAGE_SIZE-1))

static void print_usage(void)
{
	INFO ("mmap <r|w> <addr(hex)> [value(hex)]]\n");
}
int main(int argc, char *argv[])
{
    unsigned int addr, val;
    int iswrite;
    int prot;
    void* ptr;
    int fd;
    unsigned int* p;

    if (argc < 3) {
		print_usage();
        ERROR ("invalid arguments\n");
    }
    iswrite = argv[1][0] == 'r' ? 0 : 1;
    if (iswrite) {
        if (argc < 4)
            ERROR ("invalid arguments\n");
        prot = PROT_WRITE;
        val = (unsigned int)strtoul(argv[3], NULL, 16);
    } else {
        prot = PROT_READ;
    }

    addr = (unsigned int)strtoul(argv[2], NULL, 16);
    if (addr & 3) {
        ERROR ("address %x not aligned.\n", addr);
    }
  
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
        ERROR("failed to open /dev/mem\n");
    }

    //ptr = mmap((void*)(addr & PAGE_MASK), 0x10, prot, MAP_SHARED, fd, (addr & (~PAGE_MASK)));
    ptr = mmap(NULL, PAGE_SIZE, prot, MAP_SHARED, fd, addr & PAGE_MASK);
    if (ptr == (void*)(-1)) {
        ERROR ("Failed to mmap\n");
    }

    p = ptr + (addr & (~PAGE_MASK));
    if (iswrite) {
        INFO ("write %08x := %08x\n", addr, val);
        *p = val;
    } else {
        val = *p;
        INFO ("read %08x := %08x\n", addr, val);
    }

    munmap(ptr, 0x10);
    close(fd);

    return 0;
}

