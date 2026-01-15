#include "ttoolbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "msr_flipper.h"


int main(int argc, char* argv[]) {
    int fd, status;
    volatile unsigned char* sync_page;
    struct msr_flipper_arg arg = {.id = 0x1a4, .mask = 1, .value = 0, .rc = 0, .reps = 2, .core = get_current_core_id()};

    if (argc >= 2) {
        arg.reps = strtoul(argv[1], NULL, 10);
        if (!arg.reps)
            arg.reps = strtoul(argv[1], NULL, 16);
    }

    fd = open("/dev/" MSR_FLIPPER_FILE_NAME, O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    sync_page = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (!sync_page || sync_page == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    status = ioctl(fd, IOCTL_MSR_FLIPPER_JITTER, (unsigned long) &arg);
    if (status < 0) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }

    printf("Flipped MSR %lx Mask %lx\n%ld times\n", arg.id, arg.mask, arg.reps);

    munmap((void*) sync_page, 0x1000);
    close(fd);

    return 0;
}
