.file "control_flow.s"

.text


# A very deep call chain
.globl test_deep_call
test_deep_call:
    mov $0x1243, %rcx # Call depth 0x1243
    xor %r9, %r9
jmp .Lend1
.Lrstart:
.rept 5
    dec %rcx
    jz .Lend2
    lea (%rip), %r10
    crc32q %r10, %r9
    .byte 0xe8, 0xe, 0x0, 0x0, 0x0 # call %rip + 0xe
    lea (%rip), %r10
    crc32q %r10, %r9
    ret
.endr
    dec %rcx
    jz .Lend2
    lea (%rip), %r10
    crc32q %r10, %r9
    call .Lrstart
    lea (%rip), %r10
    crc32q %r10, %r9
    ret
.Lend2:
    ret
.Lend1:
    call .Lrstart
.globl test_deep_call_end
test_deep_call_end:
    nop
    ret


.align 256
# Test integrity of hard-to-predict indirect jumps
.globl test_indirect1
test_indirect1:
    push %rbp
    mov %rsp, %rbp

    xor %rdx, %rdx
    mov $1337, %rcx # num iterations

    lea .L2_target1(%rip), %r8
    push %r8
    lea .L2_target2(%rip), %r8
    push %r8
    lea .L2_target3(%rip), %r8
    push %r8
    lea .L2_target4(%rip), %r8
    push %r8

    mov $1337, %r9
    movq %r9, %xmm0
.L2_start:
    dec %rcx
    jz test_indirect1_end

    aesenc %xmm1, %xmm0
    aesenc %xmm1, %xmm0

    movq %xmm0, %r10
    and $0x3, %r10
    mov (%rsp, %r10, 8), %r11
    jmp *%r11

.L2_target1:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L2_start
.L2_target2:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L2_start
.L2_target3:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L2_start
.L2_target4:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L2_start

.globl test_indirect1_end
test_indirect1_end:
    nop
    leave
    ret

.align 256
# Test integrity of hard-to-predict indirect jumps
.globl test_direct1
test_direct1:
    xor %rdx, %rdx
    mov $1337, %rcx # num iterations

    mov $1337, %r9
    movq %r9, %xmm0
.L3_start:
    dec %rcx
    jz test_direct1_end

    aesenc %xmm1, %xmm0
    aesenc %xmm1, %xmm0

    movq %xmm0, %r10
    and $0x3, %r10
    jz .L3_target1
    dec %r10
    jz .L3_target2
    dec %r10
    jz .L3_target3
    jmp .L3_target2

.L3_target1:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L3_start
.L3_target2:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L3_start
.L3_target3:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L3_start
.L3_target4:
    lea (%rip), %r12
    crc32q %r12, %rdx
    jmp .L3_start

.globl test_direct1_end
test_direct1_end:
    nop
    ret

.align 256
.globl test_stack1
test_stack1:
    enter $0, $0x10
    crc32q %r8, %r9
    crc32q %r9, %r10
    crc32q %r10, %r11
    crc32q %r11, %r12
    crc32q %r12, %rdx
    crc32q %rdx, %rdi

    mov $1337, %rcx

    .L4_loop:

    push %r8
    push %r9w
    push %r10
    push %r11w
    pop %r8
    push %r12
    .byte 0x6a, 0xab # push (sign-extend to 64-bit 0xab)
    pop %r9
    pop %r10
    pop %r11w
    pop %rdi
    pop %dx

    dec %rcx
    jnz .L4_loop

    leave
.globl test_stack1_end
test_stack1_end:
    nop
    ret

.align 256
.globl test_rdrand1
test_rdrand1:
    rdrand %r8
    jnc test_rdrand1
.L5_1:
    rdrand %r9
    jnc .L5_1

    xor %r9, %r8
    xor %r9, %r9

    test %r8, %r8
    jnz .L5_2
    rdtsc
.L5_2:
    xor %r8, %r8

.globl test_rdrand1_end
test_rdrand1_end:
    nop
    ret

# Intentionally try to confuse the RSB
.align 256
.globl test_rsb1
test_rsb1:
    mov $0x1337, %rcx
    xor %rdx, %rdx

.L6_src1:
    lea (%rip), %r8
    crc32q %r8, %rdx
    call .L6_dest1
    lea (%rip), %r8
    crc32q %r8, %rdx
    dec %rcx
    jz test_rsb1_end
    jmp .L6_src1
.L6_src2:
    lea (%rip), %r8
    crc32q %r8, %rdx
    call .L6_dest2
    lea (%rip), %r8
    crc32q %r8, %rdx
    dec %rcx
    jz test_rsb1_end
    jmp .L6_src2

.L6_dest1:
    lea (%rip), %r8
    crc32q %r8, %rdx
    aesenc %xmm1, %xmm0
    movq %xmm0, %r10
    and $3, %r10
    test %r10, %r10
    jz .L6_dest1_divert
    ret
.L6_dest1_divert:
    pop %r10
    lea .L6_src2(%rip), %r11
    push %r11
    ret

.L6_dest2:
    lea (%rip), %r8
    crc32q %r8, %rdx
    aesenc %xmm0, %xmm1
    movq %xmm1, %r10
    and $3, %r10
    test %r10, %r10
    jz .L6_dest2_divert
    ret
.L6_dest2_divert:
    pop %r10
    lea .L6_src1(%rip), %r11
    push %r11
    ret

.globl test_rsb1_end
test_rsb1_end:
    nop
    ret

# Intentionally try to confuse the PHT
.align 256
.globl test_pht1
test_pht1:
    mov $0x3337, %rcx
    xor %rdx, %rdx

.L7_src1:
    dec %rcx
    jz test_pht1_end

    aesenc %xmm1, %xmm0
    movq %xmm0, %r10
    and $0x3f, %r10
    lea (%rip), %r8
    crc32q %r8, %rdx
    test %r10, %r10
    jnz .L7_src1

    lea (%rip), %r8
    crc32q %r8, %rdx

.L7_src2:
    dec %rcx
    jz test_pht1_end

    aesenc %xmm0, %xmm1
    movq %xmm1, %r10
    and $0x3f, %r10
    lea (%rip), %r8
    crc32q %r8, %rdx
    test %r10, %r10
    jnz .L7_src2

    lea (%rip), %r8
    crc32q %r8, %rdx
    jmp .L7_src1

.globl test_pht1_end
test_pht1_end:
    nop
    ret

# Intentionally try to confuse the BTB
.align 256
.globl test_btb1
test_btb1:
    mov $0x3337, %rcx
    xor %rdx, %rdx

    lea .L8_src2(%rip), %r8
    push %r8
    lea .L8_src1(%rip), %r8
    push %r8

.L8_src1:
    dec %rcx
    jz test_btb1_end

    xor %r9, %r9
    mov $8, %r11

    aesenc %xmm1, %xmm0
    movq %xmm0, %r10
    and $0x3f, %r10
    lea (%rip), %r8
    crc32q %r8, %rdx
    test %r10, %r10
    cmovz %r11, %r9
    mov (%rsp, %r9), %r9
    jmp *%r9

.L8_src2:
    dec %rcx
    jz test_btb1_end

    xor %r11, %r11
    mov $8, %r9

    aesenc %xmm0, %xmm1
    movq %xmm1, %r10
    and $0x3f, %r10
    lea (%rip), %r8
    crc32q %r8, %rdx
    test %r10, %r10
    cmovz %r11, %r9
    mov (%rsp, %r9), %r9
    jmp *%r9

.globl test_btb1_end
test_btb1_end:
    nop
    ret
