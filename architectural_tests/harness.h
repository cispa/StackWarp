#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>

#define BACKUP_SIZE 0x8000
#define MAX_SEQUENCE_SIZE 0xd000

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE (1 << PAGE_SHIFT)
#endif

#define page_align(x) ((x & ~((1ul << PAGE_SHIFT)-1)) + ((x & ((1ul << PAGE_SHIFT)-1)) ? PAGE_SIZE : 0))


#ifdef __cplusplus
extern "C" {
#endif

union register_backup {
    struct {
        unsigned long rax;
        unsigned long rbx;
        unsigned long rcx;
        unsigned long rdx;
        unsigned long rsi;
        unsigned long r8;
        unsigned long r9;
        unsigned long r10;
        unsigned long r11;
        unsigned long r12;
        unsigned long r13;
        unsigned long r14;
        unsigned long rdi;
        unsigned long rbp;
        unsigned long rsp;
        unsigned long _pad_1;

        // Here be vector registers
        unsigned long _pad_2;
    } __attribute__((packed));
    unsigned char raw[BACKUP_SIZE];
};

extern unsigned int insn_harness(void);
extern void insn_harness_end(void);
extern unsigned char insn_harness_gap;
extern unsigned char insn_harness_accumulate_state;
static unsigned char *const test_sequence = &insn_harness_gap;

extern union register_backup harness_init_reg_state;
extern union register_backup harness_result_reg_state;
extern union register_backup test_stack;
extern union register_backup test_data;

const static unsigned char* fixcode = (unsigned char*) 0x77700000ul;

#ifdef __cplusplus
}
static_assert(offsetof(register_backup, _pad_2) == 128, "Your compiler seems to ignore the intended memory layout of register backups");
#else
_Static_assert(offsetof(register_backup, _pad_2) == 128, "Your compiler seems to ignore the intended memory layout of register backups");
#endif

#endif