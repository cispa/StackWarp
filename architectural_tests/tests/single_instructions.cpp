#include "ttoolbox.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include "../harness.h"
#include "instructions.h"
#include "../common.h"
#include "msr_lists.h"

#define MSR_LIST msr_list_lab73_tdx_enabled

int main() {
    unsigned char core1, core2;
    const auto harness_size = reinterpret_cast<unsigned char*>(insn_harness_end) - reinterpret_cast<unsigned char*>(insn_harness);

    // Make harness RWX so that we can JIT without worries
    if (mprotect(reinterpret_cast<void*>(insn_harness), page_align(harness_size), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        perror("mprotect");
        exit(EXIT_FAILURE);
    }

    // Initialize MSR flipper kernel module
    if (init_msrs() < 0)
        exit(EXIT_FAILURE);

    get_sibling_cores(&core1, &core2);
    printf("Running on cores %u and %u\n", core1, core2);
    pin_to_core(core1);

    // Get all instructions that run in user mode and also produce deterministic output
    // auto insns = check_all_instructions();
    // printf("\nWorking deterministic instructions: %lu\n", insns.size());

    for (const auto insn: Instruction::registry()) {
        auto disasm = disassemble(insn);
        std::vector<Instruction* (*)()> ivec;
        ivec.push_back(insn);
        InsnSequenceTest insn_test(ivec);

        if (!insn_test.is_okay()) {
            printf("skipping %s\n", disasm.c_str());
            continue;
        }

        for (auto msr: MSR_LIST) {
            while (msr.mask) {
                auto mask = msr.mask & -msr.mask;
                msr.mask &= msr.mask - 1;

                msr_flipper_arg arg = {msr.msr, mask, 0, 0, core2};

                printf("\r%32s : MSR 0x%08lx - Mask 0x%016lx", disasm.c_str(), msr.msr, mask);
                fflush(stdout);

                if (const auto result = insn_test.run_test_simple_flip(arg); result == InsnSequenceTestResult::SeqTestBehaviorChange) {
                    printf("\nBehavior Change for\n\r%32s : MSR 0x%08lx - Mask 0x%016lx\n", disasm.c_str(), msr.msr, mask);
                }
            }
        }
        printf("\n");
    }

    return 0;
}