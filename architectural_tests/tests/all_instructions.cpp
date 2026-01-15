#include "ttoolbox.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include "../harness.h"
#include "instructions.h"
#include "../common.h"
#include "msr_lists.h"

#define MSR_LIST msr_list_lab73_tdx_enabled
// #define MSR_LIST msr_list_lab61

int main() {
    unsigned char core1, core2;
    const auto harness_size = reinterpret_cast<unsigned char*>(insn_harness_end) - reinterpret_cast<unsigned char*>(insn_harness);

    srand(42);

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

    std::vector<Instruction*> chain;
    unsigned int i = 0;
    for (const auto& gen: Instruction::registry()) {
        i++;
        std::vector new_chain(chain.begin(), chain.end());
        auto disasm = disassemble(gen);
        auto insn = gen();
        new_chain.push_back(insn);

        InsnSequenceTest ntest(new_chain);
        printf("Checking %32s - (%04u/%04lu) - %d\n", disasm.c_str(), i, Instruction::registry().size(), ntest.get_last_signal());

        if (ntest.is_okay()) {
            chain.push_back(insn);
            continue;
        }

        delete insn;
    }
    printf("\nGot a chain of %lu instructions\n", chain.size());

    {
        InsnSequenceTest chain_test(chain);
        for (auto msr: MSR_LIST) {
            while (msr.mask) {
                auto mask = msr.mask & -msr.mask;
                msr.mask &= msr.mask - 1;

                msr_flipper_arg arg = {msr.msr, mask, 0, 0, core2};

                printf("\rMSR 0x%08lx - Mask 0x%016lx", msr.msr, mask);
                fflush(stdout);

                const auto result = chain_test.run_test_simple_flip(arg);
                if (result == InsnSequenceTestResult::SeqTestBehaviorChange) {
                    printf("\nBehavior Change for\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
                    continue;
                }
                if (result == InsnSequenceTestResult::SeqTestEnableDisable && chain_test.get_last_signal() != SIGUSR1) {
                    printf("\nToggled\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
                    continue;
                }
                if (result == InsnSequenceTestResult::SeqTestCrash && chain_test.get_last_signal() != SIGUSR1) {
                    printf("\nCrashed\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
                }
            }
        }
    }
    printf("\n");

    for (auto insn: chain)
        delete insn;
    chain.clear();

    i = 0;
    for (const auto& gen: Instruction::registry()) {
        i++;
        std::vector new_chain(chain.begin(), chain.end());
        auto disasm = disassemble(gen);
        auto insn = gen();
        new_chain.push_back(insn);

        MonitoringInsnSequenceTest ntest(new_chain);
        printf("Checking %32s - (%04u/%04lu) - %d\n", disasm.c_str(), i, Instruction::registry().size(), ntest.get_last_signal());

        if (ntest.is_okay()) {
            chain.push_back(insn);
            continue;
        }

        delete insn;
    }

    printf("\nGot a state monitoring chain of %lu instructions\n", chain.size());
    get_sibling_cores(&core1, &core2);
    printf("Running on cores %u and %u\n", core1, core2);
    pin_to_core(core1);

    MonitoringInsnSequenceTest chain_test(chain);
    for (auto msr: MSR_LIST) {
        while (msr.mask) {
            auto mask = msr.mask & -msr.mask;
            msr.mask &= msr.mask - 1;

            msr_flipper_arg arg = {msr.msr, mask, 0, 0, core2};

            printf("\rMSR 0x%08lx - Mask 0x%016lx", msr.msr, mask);
            fflush(stdout);

            const auto result = chain_test.run_test_simple_flip(arg);
            if (result == InsnSequenceTestResult::SeqTestBehaviorChange) {
                printf("\nBehavior Change for\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
                continue;
            }
            if (result == InsnSequenceTestResult::SeqTestEnableDisable && chain_test.get_last_signal() != SIGUSR1) {
                printf("\nToggled\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
                continue;
            }
            if (result == InsnSequenceTestResult::SeqTestCrash && chain_test.get_last_signal() != SIGUSR1) {
                printf("\nCrashed\n\rMSR 0x%08lx - Mask 0x%016lx - Signal %u\n", msr.msr, mask, chain_test.get_last_signal());
            }
        }
    }
    printf("\n");

    return 0;
}
