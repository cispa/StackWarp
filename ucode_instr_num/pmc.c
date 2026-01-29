#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <assert.h>

static int fd_perf;

// --- Perf Event Helpers ---
int event_open_raw(__u64 event, __u64 umask) {
    struct perf_event_attr attr = {
        .type = PERF_TYPE_RAW,
        .size = sizeof(struct perf_event_attr),
        .config = (umask << 8) | (event & 0xFF),
        .disabled = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };
    int fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
    if (fd < 0) {
        perror("perf_event_open failed (check sudo/permissions)");
        exit(1);
    }
    return fd;
}

static inline uint64_t read_perf() {
    uint64_t val;
    if (read(fd_perf, &val, sizeof(val)) < (ssize_t)sizeof(val)) return 0;
    return val;
}

// Measurement wrapper to isolate the function call
__attribute__((noinline,aligned(0x1000))) uint64_t measure_asm(void (*asm_func)()) {
    ioctl(fd_perf, PERF_EVENT_IOC_RESET, 0);
    asm volatile("mfence\nmfence\nmfence\nmfence\n");
    ioctl(fd_perf, PERF_EVENT_IOC_ENABLE, 0);

    asm_func();
    
    ioctl(fd_perf, PERF_EVENT_IOC_DISABLE, 0);
    asm volatile("mfence\nmfence\nmfence\nmfence\n");
    
    return read_perf();
}

// --- Test Case Generation ---
// Using your corrected instruction pairs to maintain register/stack state
#define GEN_TEST(name, base_asm, inst_asm) \
    __attribute__((naked)) void func_##name##_base() { \
        asm volatile("push rbp\n\tmov rbp, rsp\n\t" \
          base_asm \
          "\n\tmfence\nmfence\nmfence\nmfence\n \
          mov rsp, rbp\n\tpop rbp\n\t \
          ret\n\t"); \
    } \
    __attribute__((naked)) void func_##name##_test() { \
        asm volatile("push rbp\n\tmov rbp, rsp\n\t" \
        inst_asm \
        "\n\tmfence\nmfence\nmfence\nmfence\n \
        mov rsp, rbp\n\tpop rbp\n\t \
        ret\n\t"); \
    }

GEN_TEST(push_reg, "", "push rax\n\t")
GEN_TEST(pop_reg,  "sub rsp, 8\n\tpop rax", "sub rsp, 0x10\n\tpop rax\n\tpop rax")

GEN_TEST(mov_rsp,  "push rax\n\t", "push rax\n\tmov rsp, rdi")
GEN_TEST(add_rsp,  "", "sub rsp, 8\n\t")

GEN_TEST(sub_rbp,  "add rbp, 8\n\tsub rbp, 8", "add rbp, 8\n\tadd rbp, 8\n\tsub rbp, 0x10")
GEN_TEST(add_rcx,  "add rax, 8\n\tsub rax, 8", "add rax, 8\n\tadd rax, 8\n\tsub rax, 0x10")

GEN_TEST(mfence,   "", "mfence")

typedef struct {
    const char* label;
    void (*base_fn)();
    void (*test_fn)();
} test_suite_t;

int main() {
    // 0x1c1: Retired Microcoded Instructions
    fd_perf = event_open_raw(0x1c1, 0x0);
    
    test_suite_t suite[] = {
        {"push REG", func_push_reg_base, func_push_reg_test},
        {"pop REG", func_pop_reg_base, func_pop_reg_test},
        {"mov rsp, rbp",       func_mov_rsp_base,  func_mov_rsp_test},
        {"add rsp / sub rsp",  func_add_rsp_base,  func_add_rsp_test},
        {"add rbp / sub rbp",  func_sub_rbp_base,  func_sub_rbp_test},
        {"add rcx / sub rcx",  func_add_rcx_base,  func_add_rcx_test},
        {"mfence (extra)",     func_mfence_base,   func_mfence_test},
    };

    printf("%-22s | %-6s | %-6s | %-6s\n", "Instruction Pair", "Base", "Total", "Delta");
    printf("----------------------------------------------------------\n");

    for (int i = 0; i < sizeof(suite)/sizeof(suite[0]); i++) {
        // measure_asm(suite[i].base_fn);
        uint64_t base = measure_asm(suite[i].base_fn);

        // measure_asm(suite[i].test_fn);
        uint64_t total = measure_asm(suite[i].test_fn);

        int64_t delta = (int64_t)total - (int64_t)base;
        
        printf("%-22s | %-6lu | %-6lu | %-6ld\n", suite[i].label, base, total, delta);
    }

    close(fd_perf);
    return 0;
}