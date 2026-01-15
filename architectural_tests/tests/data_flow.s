.file "data_flow.s"
.text

.globl test_load_store_1
test_load_store_1:
    mov $1337, %r9
    mov $0x11137, %rcx
.L1_loop:
    mov %r9, (%rsi)
    mov (%rsi), %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L1_loop
.globl test_load_store_1_end
test_load_store_1_end:
    nop
    ret

.globl test_load_store_2
test_load_store_2:
    mov $1337, %r9
    mov $0x11137, %rcx
.L2_loop:
    mov %r9, (%rsi)
    clflush (%rsi)
    mfence
    mov (%rsi), %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L2_loop
.globl test_load_store_2_end
test_load_store_2_end:
    nop
    ret

.globl test_load_store_3
test_load_store_3:
    mov $1337, %r9
    mov $0x11137, %rcx
.L3_loop:
    push %r9
    pop %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L3_loop
.globl test_load_store_3_end
test_load_store_3_end:
    nop
    ret

.globl test_load_store_4
test_load_store_4:
    mov $1337, %r9
    mov $0x11137, %rcx
.L4_loop:
    push %r9
    clflush (%rsp)
    mfence
    pop %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L4_loop
.globl test_load_store_4_end
test_load_store_4_end:
    nop
    ret

.globl test_load_store_5
test_load_store_5:
    mov $1337, %r9
    mov $0x11137, %rcx
    and $0xffffffffffffff80, %rsp
    sub $1, %rsp
.L5_loop:
    push %r9
    pop %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L5_loop
.globl test_load_store_5_end
test_load_store_5_end:
    nop
    ret

.globl test_load_store_6
test_load_store_6:
    mov $1337, %r9
    mov $0x11137, %rcx
    and $0xffffffffffffff80, %rsp
    sub $1, %rsp
.L6_loop:
    push %r9
    clflush (%rsp)
    mfence
    pop %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L6_loop
.globl test_load_store_6_end
test_load_store_6_end:
    nop
    ret

.globl test_self_modifying_1
test_self_modifying_1:
    mov $1337, %r9
    mov $0x11137, %rcx
    and $0xffffffffffffff80, %rsp
    sub $1, %rsp
.L7_loop:
    mov %r9, (.L7_target + 2)(%rip)
.L7_target:
    mov $0x1234567890abcdef, %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L7_loop
.globl test_self_modifying_1_end
test_self_modifying_1_end:
    nop
    ret

.globl test_self_modifying_2
test_self_modifying_2:
    mov $1337, %r9
    mov $0x11137, %rcx
    and $0xffffffffffffff80, %rsp
    sub $1, %rsp
.L8_loop:
    mov %r9, (.L8_target + 2)(%rip)
    clflush (.L8_target + 2)(%rip)
    mfence
.L8_target:
    mov $0x1234567890abcdef, %r10
    crc32q %r10, %r9
    dec %rcx
    jnz .L8_loop
.globl test_self_modifying_2_end
test_self_modifying_2_end:
    nop
    ret

.globl test_evict_1
test_evict_1:
    mov $1337, %r9
    mov %r15, %rdi
    mov $13337, %r8
.L9_loop:
    mov %r9, (%rdi)
    mov $(24 * 0x1000), %rcx
.L9_evict:
    mov %rcx, (%rdi, %rcx, 1)
    sub $0x1000, %rcx
    jnz .L9_evict
    crc32q (%rdi), %r9
    dec %r8
    jnz .L9_loop
.globl test_evict_1_end
test_evict_1_end:
    nop
    ret

.globl test_evict_2
test_evict_2:
    mov $1337, %r9
    mov %r15, %rdi
    mov $13337, %r8
.La_loop:
    mov %r9, (%rdi)
    mov $(32 * (0x40 * 0x800)), %rcx
.La_evict:
    mov %rcx, (%rdi, %rcx, 1)
    sub $(0x40 * 0x800), %rcx
    jnz .La_evict
    crc32q (%rdi), %r9
    dec %r8
    jnz .La_loop
.globl test_evict_2_end
test_evict_2_end:
    nop
    ret

.globl test_evict_3
test_evict_3:
    mov $1337, %r9
    mov %r15, %rdi
    mov $13337, %r8
.Lb_loop:
    mov %r9, (%rdi)
    mov $(24 * 0x1000), %rcx
.Lb_evict:
    mov %rcx, (%rdi, %rcx, 1)
    sub $0x1000, %rcx
    prefetcht1 (%rdi)
    jnz .Lb_evict
    crc32q (%rdi), %r9
    dec %r8
    jnz .Lb_loop
.globl test_evict_3_end
test_evict_3_end:
    nop
    ret

.globl test_string_set_1
test_string_set_1:
    mov $0xa0, %rsi
.Lc_loop:
    lea -0x1000(%rsp), %rdi
    mov $0x1200, %rcx
    mov $0xab, %rax

    rep stosl
    dec %rax
    dec %rsi
    jnz .Lc_loop
.globl test_string_set_1_end
test_string_set_1_end:
    nop
    ret

.globl test_string_move_1
test_string_move_1:
    mov $0xde, %r8
.Ld_loop:
    mov %r9, %rsi
    lea -0x1000(%rsp, %r8, 1), %rdi
    mov $0x1200, %rcx

    rep movsb

    mov %r9, %rdi
    mov $0x1200, %rcx
    mov %r8, %rax
    rep stosb

    dec %r8
    jnz .Ld_loop
.globl test_string_move_1_end
test_string_move_1_end:
    nop
    ret
