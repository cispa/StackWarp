#ifndef MSR_FLIPPER_H
#define MSR_FLIPPER_H

#define MSR_FLIPPER_FILE_NAME "msr_flipper"

#ifdef __cplusplus
extern "C" {
#endif

struct msr_flipper_arg {
    unsigned long id;
    unsigned long mask;
    unsigned long value;
    unsigned long rc;
    unsigned long reps;
    unsigned char core;
} __attribute__((packed));

#ifdef __cplusplus
static_assert
#else
_Static_assert
#endif
    (sizeof(struct msr_flipper_arg) == 41, "Check your compiler");

#define MSR_FLIPPER_TYPE 0xc8
#define IOCTL_MSR_FLIPPER_RD            _IOWR(MSR_FLIPPER_TYPE, 1, struct msr_flipper_arg)
#define IOCTL_MSR_FLIPPER_WR            _IOWR(MSR_FLIPPER_TYPE, 2, struct msr_flipper_arg)
#define IOCTL_MSR_FLIPPER_FLIP          _IOWR(MSR_FLIPPER_TYPE, 3, struct msr_flipper_arg)
// Flip bits at mask rapidly
#define IOCTL_MSR_FLIPPER_JITTER        _IOWR(MSR_FLIPPER_TYPE, 4, struct msr_flipper_arg)
// Same as IOCTL_MSR_FLIPPER_JITTER, but sync with user through shared memory
#define IOCTL_MSR_FLIPPER_JITTER_AWAIT  _IOWR(MSR_FLIPPER_TYPE, 5, struct msr_flipper_arg)
#define IOCTL_MSR_FLIPPER_FLIP_SINGLE   _IOWR(MSR_FLIPPER_TYPE, 6, struct msr_flipper_arg)
#define IOCTL_MSR_FLIPPER_FLIP_BUGGY    _IOWR(MSR_FLIPPER_TYPE, 7, struct msr_flipper_arg)
#define IOCTL_MSR_FLIPPER_JITTER_AWAIT_SLOW    _IOWR(MSR_FLIPPER_TYPE, 8, struct msr_flipper_arg)

#ifdef __cplusplus
}
#endif
#endif