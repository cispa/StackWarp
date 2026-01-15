#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <assert.h>

static int fd_event_raw;

int event_open(enum perf_type_id type, __u64 config, __u64 exclude_kernel, __u64 exclude_hv, __u64 exclude_callchain_kernel)
{
  static struct perf_event_attr attr;
  attr.type = type;
  attr.config = config;
  attr.size = sizeof(attr);
  attr.exclude_kernel = exclude_kernel;
  attr.exclude_hv = exclude_hv;
  attr.exclude_callchain_kernel = exclude_callchain_kernel;

  int fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, 0);
  assert(fd >= 0 && "perf_event_open failed: you forgot sudo or you have no perf event interface available for the userspace.");

  return fd;
}

int event_open_raw(enum perf_type_id type, __u64 event_selector, __u64 event_mask, __u64 exclude_kernel, __u64 exclude_hv, __u64 exclude_callchain_kernel)
{
  return event_open(type, (event_mask << 8) | event_selector, exclude_kernel, exclude_hv, exclude_callchain_kernel);
}

int event_enable(int fd)
{
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}

int event_reset(int fd)
{
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
}

int event_disable(int fd)
{
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
}

void f2(){}

__attribute__((naked)) void f1(void *p, void *p2, void *p3) {
        asm volatile("push rbp\n\t");
        asm volatile("mov rbp, rsp\n\t");
        asm volatile("mfence\nmfence\nmfence\nmfence\n");

        /*
        asm volatile("push rdi\n\t");
        asm volatile("push rsi\n\t");
        asm volatile("push rdx\n\t");
        asm volatile("sub rsp, 8\n\t");
        */

        asm volatile("mfence\nmfence\nmfence\nmfence\n");
        asm volatile("mov rsp, rbp\n\t");

        asm volatile("pop rbp\n");
        asm volatile("ret\n\t");
}

int main() {
        // 0x1c1 - Retired Microcoded Instructions
        // 0x1c2 - Retired Microcoded Ops
        fd_event_raw = event_open_raw(PERF_TYPE_RAW, 0x1c1, 0xff, 1, 1, 1);
        event_enable(fd_event_raw);
        event_reset(fd_event_raw);

        f1(&f2, &f2, &f2);
        event_disable(fd_event_raw);
        size_t result = 0;
        if (read(fd_event_raw, &result, sizeof(result)) < (size_t)sizeof(result))
        {
                return;
        }
        printf("PMC res:%lu", result);
}
