#include <iostream>
#include <fstream>
#include <memory>
#include <zstd.h>
#include <sys/ucontext.h>
#include <csignal>
#include <cstring>
#include <capstone/capstone.h>
#include "harness.h"
#include "common.h"

// ANSI color codes for highlighting differences
const char* RESET = "\033[0m";
const char* RED = "\033[31m";

void print_register_diff(const char* reg_name, unsigned long pre, unsigned long post) {
    if (pre != post) {
        std::cout << RED << reg_name << ": 0x" << std::hex << pre << " -> 0x" << post << RESET << std::endl;
    } else {
        std::cout << reg_name << ": 0x" << std::hex << pre << " -> 0x" << post << std::endl;
    }
}

void compare_register_states(const register_backup& pre, const register_backup& post) {
    std::cout << "Register State Comparison:\n";
    print_register_diff("RAX", pre.rax, post.rax);
    print_register_diff("RBX", pre.rbx, post.rbx);
    print_register_diff("RCX", pre.rcx, post.rcx);
    print_register_diff("RDX", pre.rdx, post.rdx);
    print_register_diff("RSI", pre.rsi, post.rsi);
    print_register_diff("R8", pre.r8, post.r8);
    print_register_diff("R9", pre.r9, post.r9);
    print_register_diff("R10", pre.r10, post.r10);
    print_register_diff("R11", pre.r11, post.r11);
    print_register_diff("R12", pre.r12, post.r12);
    print_register_diff("R13", pre.r13, post.r13);
    print_register_diff("R14", pre.r14, post.r14);
    print_register_diff("RDI", pre.rdi, post.rdi);
    print_register_diff("RBP", pre.rbp, post.rbp);
    print_register_diff("RSP", pre.rsp, post.rsp);
}

void compare_register_states_ucontext(const register_backup &pre, const ucontext_t &post) {
    std::cout << "Register State Comparison:\n";
    print_register_diff("RAX", pre.rax, post.uc_mcontext.gregs[REG_RAX]);
    print_register_diff("RBX", pre.rbx, post.uc_mcontext.gregs[REG_RBX]);
    print_register_diff("RCX", pre.rcx, post.uc_mcontext.gregs[REG_RCX]);
    print_register_diff("RDX", pre.rdx, post.uc_mcontext.gregs[REG_RDX]);
    print_register_diff("RSI", pre.rsi, post.uc_mcontext.gregs[REG_RSI]);
    print_register_diff("R8", pre.r8, post.uc_mcontext.gregs[REG_R8]);
    print_register_diff("R9", pre.r9, post.uc_mcontext.gregs[REG_R9]);
    print_register_diff("R10", pre.r10, post.uc_mcontext.gregs[REG_R10]);
    print_register_diff("R11", pre.r11, post.uc_mcontext.gregs[REG_R11]);
    print_register_diff("R12", pre.r12, post.uc_mcontext.gregs[REG_R12]);
    print_register_diff("R13", pre.r13, post.uc_mcontext.gregs[REG_R13]);
    print_register_diff("R14", pre.r14, post.uc_mcontext.gregs[REG_R14]);
    print_register_diff("RDI", pre.rdi, post.uc_mcontext.gregs[REG_RDI]);
    print_register_diff("RBP", pre.rbp, post.uc_mcontext.gregs[REG_RBP]);
    print_register_diff("RSP", pre.rsp, post.uc_mcontext.gregs[REG_RSP]);
}

void print_instructions(const unsigned char* code, size_t code_size) {
    csh handle;
    cs_insn *insn;
    size_t count;

    if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) {
        std::cerr << "Failed to initialize capstone" << std::endl;
        return;
    }

    count = cs_disasm(handle, code, code_size, 0x1000, 0, &insn);
    if (count > 0) {
        std::cout << "Instruction Sequence:\n";
        for (size_t j = 0; j < count; j++) {
            std::cout << std::hex << insn[j].address << ": "
                     << insn[j].mnemonic << " " << insn[j].op_str << std::endl;
        }
        cs_free(insn, count);
    } else {
        std::cerr << "Failed to disassemble code" << std::endl;
    }

    cs_close(&handle);
}

int main(int argc, char* argv[]) {

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <compressed result>" << std::endl;
        return 1;
    }

    // Read compressed file
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open file: " << argv[1] << std::endl;
        return 1;
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> compressed_data(size);
    file.read(compressed_data.data(), size);

    // Get decompressed size
    size_t decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), size);
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        std::cerr << "Failed to get decompressed size" << std::endl;
        return 1;
    }

    // Decompress data
    std::vector<char> decompressed_data(decompressed_size);
    size_t result = ZSTD_decompress(decompressed_data.data(), decompressed_size,
                                  compressed_data.data(), size);
    if (ZSTD_isError(result)) {
        std::cerr << "Decompression failed: " << ZSTD_getErrorName(result) << std::endl;
        return 1;
    }

    // Cast to insn_report
    auto* report = reinterpret_cast<insn_report*>(decompressed_data.data());

    // Print signal number
    std::cout << "Signal: " << report->signal << std::endl << std::endl;

    // Disassemble and print instructions
    print_instructions(report->sequence, MAX_SEQUENCE_SIZE);
    std::cout << std::endl;

    // Compare register states
    if (report->signal == SIGUSR2) {
        compare_register_states(report->state_pre, report->state_post);
    } else {
        compare_register_states_ucontext(report->state_pre, report->uc_post);
    }

    return 0;
}