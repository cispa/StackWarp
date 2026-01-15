#include "ttoolbox.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sys/mman.h>
#include "../harness.h"
#include "instructions.h"
#include "msr_lists.h"
#include "../common.h"
#include "../ptedit_header.h"

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
mk_test(string_set_1)
mk_test(string_move_1)

#define IA32_PQR_ASSOC 0xc8f
#define IA32_L2_MASK_0 0xd10
#define IA32_L3_MASK_0 0xc90

#define TARGET_CLOS_ID 1

static int cat_info(unsigned int& l2_capmask_len, unsigned int& l3_capmask_len) {
    unsigned int a, b, c, d;

    // Check for L2 and L3 CAT support
    __cpuid_count(0x10, 0, a, b, c, d);
    if ((b & 0x6) != 0x6)
        return -1;

    __cpuid_count(0x10, 1, a, b, c, d);
    l3_capmask_len = (a & 0x1f) + 1;

    __cpuid_count(0x10, 2, a, b, c, d);
    l2_capmask_len = (a & 0x1f) + 1;

    return 0;
}

int main() {
    unsigned char core1, core2;
    const auto harness_size = reinterpret_cast<unsigned char*>(insn_harness_end) - reinterpret_cast<unsigned char*>(insn_harness);
    unsigned int l2_capmask_len, l3_capmask_len;
    unsigned long clos = 0;

    if (getuid() != 0) {
        puts("Give me root privileges!\n");
        exit(EXIT_FAILURE);
    }

    if (cat_info(l2_capmask_len, l3_capmask_len) < 0) {
        printf("This CPU does not support Intel RDT with CAT\n");
        exit(EXIT_FAILURE);
    }

    printf("Got CAT with %u L2 capacity bits and %u L3 capacity bits\n", l2_capmask_len, l3_capmask_len);

    // Make harness RWX so that we can JIT without worries
    if (mprotect(reinterpret_cast<void*>(insn_harness), page_align(harness_size), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        perror("mprotect");
        exit(EXIT_FAILURE);
    }

    // Initialize MSR flipper kernel module
    if (init_msrs() < 0)
        exit(EXIT_FAILURE);

    for (unsigned int i = 0; i < BACKUP_SIZE / 0x1000; i+= 0x1000)
        asm volatile ("mov (%0), %1" :: "r"(test_stack.raw + i), "r"(1));

    for (unsigned int i = 0; i < BACKUP_SIZE / 0x1000; i+= 0x1000)
        asm volatile ("mov (%0), %1" :: "r"(test_data.raw + i), "r"(1));

    get_sibling_cores(&core1, &core2);
    printf("Running on cores %u and %u\n", core1, core2);

    //read_msr(core2, IA32_PQR_ASSOC, &clos);
    //clos |= (unsigned long) TARGET_CLOS_ID << 32;
    //write_msr(core2, IA32_PQR_ASSOC, clos);

    pin_to_core(core1);

    static const struct msr_mask msr_list[] = {
        {IA32_L2_MASK_0 + TARGET_CLOS_ID, (1ul << l2_capmask_len) - 1},
        {IA32_L3_MASK_0 + TARGET_CLOS_ID, (1ul << l3_capmask_len) - 1},
    };

    write_msr(core2, IA32_L2_MASK_0 + TARGET_CLOS_ID, (1ul << (l2_capmask_len / 2)) - 1);
    write_msr(core2, IA32_L3_MASK_0 + TARGET_CLOS_ID, (1ul << (l3_capmask_len / 2)) - 1);

    while (1) {
        for (const auto test: test_registry()) {
            auto insn_test = test.first();

            if (!insn_test->is_okay()) {
                printf("skipping %s\n", test.second.c_str());
                delete insn_test;
                continue;
            }

            for (auto msr: msr_list) {
                const unsigned long mask = msr.mask;
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
            printf("\n");
            delete insn_test;
        }
    }

    return 0;
}