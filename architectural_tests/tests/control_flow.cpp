#include "ttoolbox.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sys/mman.h>
#include "../harness.h"
#include "instructions.h"
#include "../common.h"
#include "msr_lists.h"

// #define MSR_LIST msr_list_lab73_tdx_enabled
#define MSR_LIST msr_subjects
// #define MSR_LIST msr_list_lab61

static inline std::vector<std::pair<InsnSequenceTest*(*)(), std::string>> &test_registry() {
    static std::vector<std::pair<InsnSequenceTest*(*)(), std::string>> registry;
    return registry;
}

#define mk_test(x) \
extern "C" void test_##x(); \
extern "C" void test_##x##_end(); \
static InsnSequenceTest* mk_##x() { \
return new InsnSequenceTest (reinterpret_cast<unsigned char*>(test_##x), reinterpret_cast<unsigned char*>(test_##x##_end) - reinterpret_cast<unsigned char*>(test_##x)); \
} \
namespace { struct test_registrar_##x { test_registrar_##x() { test_registry().emplace_back(mk_##x, std::string(#x)); }} registrar_instance_##x; }

mk_test(deep_call)
mk_test(indirect1)
mk_test(direct1)
mk_test(stack1)
mk_test(rdrand1)
mk_test(rsb1)
mk_test(pht1)
mk_test(btb1)
mk_test(load_store_1)
// mk_test(load_store_2)
mk_test(load_store_3)
// mk_test(load_store_4)
mk_test(load_store_5)
// mk_test(load_store_6)
mk_test(self_modifying_1)
// mk_test(self_modifying_2)
mk_test(evict_1)
mk_test(evict_2)
mk_test(evict_3)

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

    for (const auto test: test_registry()) {
        auto insn_test = test.first();

        if (!insn_test->is_okay()) {
            printf("skipping %s\n", test.second.c_str());
            delete insn_test;
            continue;
        }

        for (auto msr: MSR_LIST) {
            while (msr.mask) {
                auto mask = msr.mask & -msr.mask;
                msr.mask &= msr.mask - 1;

                // auto mask = 0u;//msr.mask;
                // msr.mask = 0;

                msr_flipper_arg arg = {msr.msr, mask, mask, 0, core2};

                printf("\r%32s : MSR 0x%08lx - Mask 0x%016lx", test.second.c_str(), msr.msr, mask);
                fflush(stdout);
                sched_yield();

                const auto result = insn_test->run_test_jitter(arg);
                if (result == InsnSequenceTestResult::SeqTestBehaviorChange) {
                    printf("\nBehavior Change for\n\r%32s : MSR 0x%08lx - Mask 0x%016lx\n", test.second.c_str(), msr.msr, mask);
                    continue;
                }
                if (result == InsnSequenceTestResult::SeqTestCrash && insn_test->get_last_signal() != SIGUSR1) {
                    printf("\nCrash for\n\r%32s : MSR 0x%08lx - Mask 0x%016lx - Signal %u\n", test.second.c_str(), msr.msr, mask, insn_test->get_last_signal());
                    continue;
                }
                if (result == InsnSequenceTestResult::SeqTestEnableDisable && insn_test->get_last_signal() != SIGUSR1) {
                    printf("\nToggled\n\r%32s : MSR 0x%08lx - Mask 0x%016lx - Signal %u\n", test.second.c_str(), msr.msr, mask, insn_test->get_last_signal());
                }
            }
        }
        printf("\n");
        delete insn_test;
    }

    return 0;
}