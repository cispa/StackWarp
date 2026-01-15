.file "harness.s"

.set backup_size, 0x8000
.set large_buffer_size, (1 << 24)

.macro save_state, dest
    lea \dest, %r15

    # Save GP registers
    mov %rax, 0(%r15)
    mov %rbx, 8(%r15)
    mov %rcx, 16(%r15)
    mov %rdx, 24(%r15)
    mov %rsi, 32(%r15)
    mov %r8,  40(%r15)
    mov %r9,  48(%r15)
    mov %r10, 56(%r15)
    mov %r11, 64(%r15)
    mov %r12, 72(%r15)
    mov %r13, 80(%r15)
    mov %r14, 88(%r15)
    mov %rdi, 96(%r15)
    mov %rbp, 104(%r15)
    mov %rsp, 112(%r15)

    # XSAVE (at offset 128, keeping space for GPRs)
    lea \dest, %rsi
    add $128, %rsi
    xor %ecx, %ecx          # ECX = 0 (XSAVE legacy area)
    xor %edx, %edx          # EDX:EAX = feature mask = 0xFFFFFFFF (for full)
    mov $-1, %eax
    xsave (%rsi)
.endm

.macro restore_state, src
    lea \src, %rdi

    # Restore GP registers
    mov 0(%rdi), %rax
    mov 8(%rdi), %rbx
    mov 16(%rdi), %rcx
    mov 24(%rdi), %rdx
    mov 32(%rdi), %rsi
    mov 40(%rdi), %r8
    mov 48(%rdi), %r9
    mov 56(%rdi), %r10
    mov 64(%rdi), %r11
    mov 72(%rdi), %r12
    mov 80(%rdi), %r13
    mov 88(%rdi), %r14
    mov 96(%rdi), %r15
    mov 104(%rdi), %rbp
    mov 112(%rdi), %rsp

    # XRSTOR to restore FP/SIMD state
    lea \src, %rsi
    add $128, %rsi
    xor %ecx, %ecx
    xor %edx, %edx
    mov $-1, %eax
    xrstor (%rsi)
.endm

.data
.extern sync_page

.section .fixdata, "aw"
// For cache eviction etc.
.align 0x1000
.Llarge_buffer:
.skip large_buffer_size

# Reserve 32kB for state backup
.align 0x1000
.Lstate_backup:
.skip backup_size


# Reserve 32kB for initial register state
.globl harness_init_reg_state
harness_init_reg_state:
.skip backup_size

# Reserve 32kB for result register state
.align 0x1000
.globl harness_result_reg_state
harness_result_reg_state:
.skip backup_size

# Reserve 32kB for the stack
.globl test_stack
test_stack:
.skip backup_size

# Reserve 32kB for other stuff
.globl test_data
test_data:
.skip backup_size

.section .fixcode, "ax"
.align 0x1000
// unsigned int insn_harness(void); // Returns the CRC32 checksum of the register state after executing it
.globl insn_harness
insn_harness:
    # Clean the test data - initialize every qword with the address of its successor element
    mov $(backup_size / 8), %rcx
    lea test_stack(%rip), %rsi
    lea test_data(%rip), %rdi
    mov %rdi, %rax
    xor %rdx, %rdx
    .Linit_data:
    add $8, %rax
    movq %rax, (%rsi, %rdx, 8)
    movq %rax, (%rdi, %rdx, 8)
    inc %rdx
    dec %rcx
    jnz .Linit_data

    # Backup our register state so that we don't break the rest of the program
    save_state .Lstate_backup(%rip)
    
    # Initialize result state, set registers to constant value
    lea harness_init_reg_state(%rip), %rsi
    lea harness_result_reg_state(%rip), %rdi
    mov $backup_size, %rcx
    rep movsb
    restore_state harness_result_reg_state(%rip)
    mov (test_stack + 0x4000)(%rip), %rsp

    # Tell the kernel module to start flipping MSRs if it waits for us
    mov sync_page(%rip), %r15
    movb $1, 1(%r15)
    lea .Llarge_buffer(%rip), %r15
    mfence
    
    // Padding
    .fill 0x1000, 1, 0x90

    # Make some room for an instruction sequence here
.globl insn_harness_gap
insn_harness_gap:
    .fill 0xd000, 1, 0x90
    .align 0x100

    # Tell kernel module to stop
    mov sync_page(%rip), %r15
    movb $0, 1(%r15)
    xor %r15, %r15
    mfence

    # Save result state and recover original register state
    save_state harness_result_reg_state(%rip)
    restore_state .Lstate_backup(%rip)

    xor %rax, %rax
    movq %rax, (harness_result_reg_state + 112)(%rip) # Clear saved stack pointer

    # Return CRC32 checksum of saved register state, stack and data
    xor %rax, %rax
    mov $(3 * backup_size / 8), %rcx
    xor %rdi, %rdi
    lea harness_result_reg_state(%rip), %rsi
    .Lcrc:
    mov (%rsi, %rdi, 8), %rdx
    crc32q %rdx, %rax
    inc %rdi
    dec %rcx
    jnz .Lcrc

    ret

.globl insn_harness_accumulate_state
insn_harness_accumulate_state:
    save_state harness_result_reg_state(%rip)
    # Return CRC32 checksum of saved register state, stack and data
    xor %rax, %rax
    mov $(3 * backup_size / 8), %rcx
    xor %rdi, %rdi
    lea harness_result_reg_state(%rip), %rsi
    .Lcrc2:
    mov (%rsi, %rdi, 8), %rdx
    crc32q %rdx, %rax
    inc %rdi
    dec %rcx
    jnz .Lcrc2
    restore_state harness_result_reg_state(%rip)
    ret

.globl insn_harness_end
insn_harness_end:
    ret

.globl init_state_xsave
init_state_xsave:
    vzeroall

    aesenc %xmm0, %xmm0
    vinserti128 $1, %xmm0, %ymm0, %ymm0
    vpaddq %ymm0, %ymm0, %ymm1
    vpaddq %ymm0, %ymm1, %ymm2
    vpaddq %ymm0, %ymm2, %ymm3
    vpaddq %ymm0, %ymm3, %ymm4
    vpaddq %ymm0, %ymm4, %ymm5
    vpaddq %ymm0, %ymm5, %ymm6
    vpaddq %ymm0, %ymm6, %ymm7
    vpaddq %ymm0, %ymm7, %ymm8
    vpaddq %ymm0, %ymm8, %ymm9
    vpaddq %ymm0, %ymm9, %ymm10
    vpaddq %ymm0, %ymm10, %ymm11
    vpaddq %ymm0, %ymm11, %ymm12
    vpaddq %ymm0, %ymm12, %ymm13
    vpaddq %ymm0, %ymm13, %ymm14
    vpaddq %ymm0, %ymm14, %ymm15

    lea (128 + harness_init_reg_state)(%rip), %rsi
    xor %ecx, %ecx
    xor %edx, %edx
    mov $-1, %eax
    xsave (%rsi)

    vzeroall
    ret
