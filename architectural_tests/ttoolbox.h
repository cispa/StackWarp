// Tristan's Toolbox of Miscellaneous C functions
//
// This header contains functions for x86_64 Linux that I often reuse across projects and am too lazy to constantly rewrite.
// Most of the code in here is AI generated and untested, so use it at your own risk.
//

#ifndef TTOOLBOX_H
#define TTOOLBOX_H

#ifndef TTOOLBOXFUN
#define TTOOLBOXFUN
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <syscall.h>

#ifdef __x86_64__
#include <cpuid.h>
#endif

#ifndef str__
#define str__(x) #x
#endif
#ifndef stringify
#define stringify(x) str__(x)
#endif

#define countof(x) (sizeof(x)/sizeof(*(x)))
#ifndef __cplusplus
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif

// Define this flag to disable some functions that may not be properly implemented by libcs other than glibc
#ifdef DISABLE_NONSTANDARD
#define prctl(...)
#define sched_setaffinity(...)
#endif


static TTOOLBOXFUN unsigned int get_core_count(void) {
    FILE *fp = fopen("/sys/devices/system/cpu/present", "r");
    int start, end;

    if (!fp) {
        perror("Failed to open CPU info file");
        return 0;
    }

    if (fscanf(fp, "%d-%d", &start, &end) != 2) {
        perror("Failed to parse CPU info");
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return end - start + 1;
}

static TTOOLBOXFUN void pin_to_core(unsigned int id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(id, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);
}
#define set_processor_affinity pin_to_core

static TTOOLBOXFUN void unpin_core(void) {
    unsigned int num_cpus = get_core_count();
    unsigned int i;
    cpu_set_t set;

    CPU_ZERO(&set);
    for (i = 0; i < num_cpus; i++) {
        CPU_SET(i, &set);
    }

    sched_setaffinity(0, sizeof(cpu_set_t), &set);
}

#ifdef __x86_64__
// Gets the number of the highest supported physical address bit
static TTOOLBOXFUN unsigned int get_maxphysaddr(void) {
    unsigned long a;

    asm volatile ("cpuid" : "=a"(a) : "a"(0x80000008), "c"(0) : "rbx", "rdx");

    return (unsigned int) (a & 0xff);
}
#endif

#ifdef  __x86_64__
/**
 * Translates a virtual address to a physical address using /proc/self/pagemap.
 *
 * @param vaddr Virtual address to translate.
 * @return Physical address on success, or 0 on failure.
 */
static TTOOLBOXFUN uint64_t virt_to_phys(void *vaddr) {
    uintptr_t vaddr_num = (uintptr_t)vaddr;
    uintptr_t pfn_entry, pfn;
    off_t offset;
    int pagemap_fd;
#ifndef PAGE_SIZE
    const unsigned long PAGE_SIZE = 0x1000;
#endif
#ifndef PFN_MASK

    const unsigned long PFN_MASK = (1ul << get_maxphysaddr()) - 1;
#else
#endif

    // Open /proc/self/pagemap
    pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap_fd < 0) {
        perror("Failed to open /proc/self/pagemap");
        return 0;
    }

    // Calculate the offset in pagemap
    offset = (vaddr_num / PAGE_SIZE) * sizeof(uint64_t);

    // Seek to the right entry
    if (lseek(pagemap_fd, offset, SEEK_SET) == -1) {
        perror("lseek failed");
        close(pagemap_fd);
        return 0;
    }

    // Read the page map entry
    if (read(pagemap_fd, &pfn_entry, sizeof(uint64_t)) != sizeof(uint64_t)) {
        perror("read failed");
        close(pagemap_fd);
        return 0;
    }

    close(pagemap_fd);

    // Check if page is present
    if (!(pfn_entry & (1ULL << 63))) {
        fprintf(stderr, "Page not present in physical memory\n");
        return 0;
    }

    // Extract PFN and compute physical address
    pfn = pfn_entry & PFN_MASK;
    return (pfn * PAGE_SIZE) + (vaddr_num % PAGE_SIZE);
}
#endif

#ifdef TTOOLBOX_CPUID_CHECK
/**
 * Checks if the CPUID instruction is supported on this system.
 *
 * Returns:
 *   1 if CPUID is supported, 0 otherwise.
 */
static TTOOLBOXFUN unsigned char is_cpuid_supported(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned long supported;

    __asm__ volatile (
        "pushfq\n\t"            // Save FLAGS register
        "popq %%rax\n\t"
        "movq %%rax, %%rcx\n\t" // Copy FLAGS
        "xorq $0x200000, %%rax\n\t" // Flip the ID bit (bit 21)
        "pushq %%rax\n\t"
        "popfq\n\t"             // Load modified FLAGS
        "pushfq\n\t"
        "popq %%rax\n\t"
        "cmpq %%rcx, %%rax\n\t"
        "sete %0\n\t"           // Set 'supported' if flags changed
        "pushq %%rcx\n\t"       // Restore original FLAGS
        "popfq\n\t"
        : "=r" (supported)
        :
        : "rax", "rcx"
    );
    return (unsigned char) supported;
#else
    return 0;  // CPUID is not supported on non-x86 architectures
#endif
}
#else
static TTOOLBOXFUN unsigned char is_cpuid_supported(void) {
#if defined(__x86_64__) || defined(__i386__)
    return 1;
#else
    return 0;  // CPUID is not supported on non-x86 architectures
#endif
}
#endif

#ifdef __x86_64__
/**
 * Retrieves the ID of the CPU core the calling thread is currently running on.
 *
 * Returns:
 *   On success, returns the core ID (non-negative integer).
 *   On failure, returns -1 and prints an error message.
 */
static TTOOLBOXFUN unsigned int get_current_core_id(void) {
    unsigned int eax, ebx, ecx, edx;
    unsigned int apic_id;

    __cpuid(1, eax, ebx, ecx, edx);

    apic_id = (ebx >> 24) & 0xFF;
    return apic_id;
}

static TTOOLBOXFUN unsigned char is_avx2_supported() {
    unsigned int a, b, c, d;

    __cpuid_count(7, 0, a, b, c, d);

    // AVX2 is indicated by bit 5 of EBX from CPUID leaf 7, subleaf 0
    return (b & (1 << 5)) != 0;
}

static TTOOLBOXFUN unsigned char is_avx512_supported() {
    unsigned int a, b, c, d;

    __cpuid_count(7, 0, a, b, c, d);

    // AVX-512F is indicated by bit 16 of EBX from CPUID leaf 7, subleaf 0
    return (b & (1 << 16)) != 0;
}

static TTOOLBOXFUN unsigned int get_data_cache_set_count(unsigned int cache_level) {
    unsigned int eax, ebx, ecx, edx;
    unsigned int cache_type;
    int subleaf = 0;

    // Loop over CPUID leaf 4 subleaves.
    while (1) {
        asm volatile(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(4), "c"(subleaf)
        );
        subleaf++;

        // Bits 4:0 of EAX: cache type (0 means no more caches).
        cache_type = eax & 0x1F;

        // No further caches
        if (cache_type == 0)
            break;

        // skip the L1i cache
        if (cache_type == 2)
            continue;

        // Bits 7:5 of EAX: cache level.
        if (((eax >> 5) & 0x7) == 2)
            return ecx + 1; // number of sets
    }

    return 0;
}
#endif

static TTOOLBOXFUN int get_sibling_cores(unsigned char* core_a, unsigned char* core_b){
    const static char fmt_filepath[] = "/sys/devices/system/cpu/cpu%u/topology/thread_siblings_list";
    char filepath[sizeof(fmt_filepath) + 0x40];
    char core_str[16];
    unsigned int core_count = get_core_count(), i;
    unsigned int o1 = 0, o2 = 0;
    int rc = -1;
    size_t len;
    FILE *f;

    // Set default in case we fail somewhere
    *core_a = 0;
    *core_b = core_count / 2;

    for (i = 0; i < core_count; i++) {
        pin_to_core(i);

        snprintf(filepath, sizeof(filepath), fmt_filepath, i);
        f = fopen(filepath, "r");
        if (!f || !~(unsigned long) f)
            continue;
        len = fread(core_str, 1, sizeof(core_str), f);
        fclose(f);

        if (len >= sizeof(core_str))
            continue;

        if (sscanf(core_str, "%u-%u", &o1, &o2) == 2) {
            *core_a = (unsigned char) o1;
            *core_b = (unsigned char) o2;
            rc = 0;
            break;
        }
        if (sscanf(core_str, "%u,%u", &o1, &o2) == 2) {
            *core_a = (unsigned char) o1;
            *core_b = (unsigned char) o2;
            rc = 0;
            break;
        }
    }

    unpin_core();
    return rc;
}

static TTOOLBOXFUN void dump_memory(const void *addr, size_t length) {
    const unsigned char *p = (const unsigned char *)addr;
    unsigned char c;
    size_t i, j;

    for (i = 0; i < length; i += 16) {
        printf("%p: ", p + i);

        for (j = 0; j < 16 && i + j < length; ++j) {
            printf("%02X ", p[i + j]);
        }

        for (j = length - i; j < 16; ++j) {
            printf("   ");
        }

        printf(" | ");

        for (j = 0; j < 16 && i + j < length; ++j) {
            c = p[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }

        printf("\n");
    }
}

/**
 * Checks whether a file descriptor for a given file path is already open.
 * If open, returns the existing file descriptor; otherwise, returns -1.
 */
static TTOOLBOXFUN int get_open_fd(const char *filepath) {
    char fd_path[PATH_MAX], link_path[PATH_MAX];
    ssize_t len;
    DIR *dir;
    int ret = -1;
    struct dirent *entry;

    // Open /proc/self/fd to iterate over open file descriptors
    dir = opendir("/proc/self/fd");
    if (!dir) {
        perror("opendir");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (entry->d_name[0] == '.')
            continue;

        // Construct the path to the symbolic link in /proc/self/fd
        snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%s", entry->d_name);

        // Read the symbolic link to get the actual file path
        len = readlink(fd_path, link_path, sizeof(link_path) - 1);
        if (len == -1)
            continue;

        link_path[len] = '\0';  // Null-terminate the string

        // Compare with the target filepath
        if (strcmp(link_path, filepath) == 0) {
            ret = atoi(entry->d_name);
            break;
        }
    }

    closedir(dir);
    return ret;  // Not found
}

// Gets the canonical path of an open file descriptor
static TTOOLBOXFUN const char *get_fd_path(int fd, char *buffer, size_t size) {
    char path[PATH_MAX];
    ssize_t len;

    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

    len = readlink(path, buffer, size - 1);
    if (len == -1)
        return NULL;

    buffer[len] = '\0';
    return buffer;
}

static TTOOLBOXFUN int read_msr(unsigned int cpu, unsigned long msr, unsigned long* out) {
    char msr_path[64];
    unsigned long value;
    ssize_t status;
    int fd;

    snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", cpu);
    fd = open(msr_path, O_RDONLY);
    if (fd < 0)
        return -1;

    status = syscall(SYS_pread64, fd, &value, sizeof(value), (off_t) msr);
    close(fd);

    if (status != sizeof(value))
        return -1;

    *out = value;
    return 0;
}

static TTOOLBOXFUN int write_msr(unsigned int cpu, unsigned long msr, unsigned long val) {
    char msr_path[64];
    ssize_t status;
    int fd;

    snprintf(msr_path, sizeof(msr_path), "/dev/cpu/%d/msr", cpu);
    fd = open(msr_path, O_WRONLY);
    if (fd < 0)
        return -1;

    status = syscall(SYS_pwrite64, fd, &val, sizeof(val), (off_t) msr);
    close(fd);

    if (status != sizeof(val))
        return -1;

    return 0;
}

static TTOOLBOXFUN void die_with_parent(void) {
    prctl(PR_SET_PDEATHSIG, SIGHUP);
}

static TTOOLBOXFUN void reap_zombies(void) {
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {}
}

/* Recursively creates a directory if it does not exist yet */
static TTOOLBOXFUN int create_directory_recursively(const char *path) {
    char temp_path[1024];
    char *p = NULL;
    struct stat sb;

    // Copy path to a mutable buffer
    strncpy(temp_path, path, sizeof(temp_path));
    temp_path[sizeof(temp_path) - 1] = '\0';

    // Iterate over each directory level
    for (p = temp_path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0'; // Temporarily end the string at this directory level

            // Check if this directory exists
            if (stat(temp_path, &sb) != 0) {
                if (mkdir(temp_path, 0755) != 0) {
                    if (errno != EEXIST) {
                        perror("mkdir");
                        return -1;
                    }
                }
            } else if (!S_ISDIR(sb.st_mode))
                return -1;

            *p = '/'; // Restore separator
        }
    }

    // Final directory creation (last component)
    if (stat(temp_path, &sb) != 0) {
        if (mkdir(temp_path, 0755) != 0) {
            if (errno != EEXIST) {
                perror("mkdir");
                return -1;
            }
        }
    } else if (!S_ISDIR(sb.st_mode))
        return -1;

    return 0;
}

#endif