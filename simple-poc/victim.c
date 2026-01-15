//#define SHADOW_STACK 1  % requires environment variable for glibc: `GLIBC_TUNABLES=glibc.cpu.hwcaps=SHSTK`

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#ifdef SHADOW_STACK
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#endif

#define REP 100
#define SHM_NAME "ht_toggle_shm"

#define POPULATE_RET  ".rept 100\n" \
        "push %%rax\n" \
        ".endr\n"
#define UNPOPULATE_RET "add $800, %%rsp\n"

volatile uint32_t *flag;

void unreachable_func() {
    *flag = 0;
    asm volatile("clflush (%0); mfence\n" ::"r"(flag));

    printf("Unreachable!!!\n");
    exit(0);
}

int main() {
#ifdef SHADOW_STACK
    syscall(SYS_arch_prctl, ARCH_SHSTK_ENABLE, ARCH_SHSTK_SHSTK);
#endif
    shm_unlink(SHM_NAME);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    if (ftruncate(shm_fd, sizeof(uint32_t)) != 0) {
        perror("ftruncate");
        return EXIT_FAILURE;
    }
    flag = mmap(NULL, sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (flag == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    *flag = 0;


    struct sched_param param;
    // priority between 1 and 99
    param.sched_priority = 99;

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("[WARNING] Could not set priority. Run as root to reduce the chances of the system crashing\n");
    }

    // preload unreachable function
    *((volatile char*)unreachable_func);

    if (mlockall(MCL_CURRENT) == -1) {
        perror("[WARNING] Could not lock all memory. Run as root to reduce the chances of the system crashing\n");
    }

    sched_yield();

    printf("Let's go!\n");
    sched_yield();

    while (1) {
        // start interesting section
        *flag = 1;
        asm volatile("clflush (%0); mfence\n" ::"r"(flag));
        asm volatile(
        POPULATE_RET
        "call 1f\n"
        UNPOPULATE_RET
        "jmp 2f\n"

        "1:\n"
        POPULATE_RET
        UNPOPULATE_RET
        "ret\n"
        "2:\n"
        ::"a"(unreachable_func));
        // and stop
        *flag = 0;
        asm volatile("clflush (%0); mfence\n" ::"r"(flag));

        // for(int i = 0; i < 1000; i++) asm volatile("nop");
    }

    munmap(flag, sizeof(uint32_t));
    shm_unlink(SHM_NAME);
    return EXIT_SUCCESS;
}
