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

// MTRR MSR addresses
#define IA32_MTRRCAP           0xfe
#define IA32_MTRR_PHYSBASE0    0x200
#define IA32_MTRR_PHYSMASK0    0x201
#define MTRR_MEMTYPE_MASK      0xFF
#define MTRR_VALID_BIT         (1UL << 11)

#define MTRR_PHYSMASK_VALID (1ULL << 11)
#define MTRR_MASK_BASE_BIT 12

enum mtype : unsigned char {
    UC = 0,
    WC,
    WT = 4,
    WP,
    WB,
};

#define MTYPE1 mtype::WB
#define MTYPE2 mtype::WP

static unsigned char get_num_mtrrs() {
    unsigned long cap = 0;

    if (read_msr(0, IA32_MTRRCAP, &cap) < 0)
        return 0;

    return static_cast<unsigned char>(cap & 0xff);
}

static unsigned long compute_mtrr_range_size(unsigned long physmask, unsigned char maxphyaddr) {
    unsigned long mask;
    unsigned char mask_bits, trailing_zeros;

    // Check if the MTRR is valid
    if ((physmask & MTRR_PHYSMASK_VALID) == 0) {
        return 0; // MTRR is disabled
    }

    // Extract the address mask bits
    mask = physmask >> MTRR_MASK_BASE_BIT;
    mask_bits = maxphyaddr - MTRR_MASK_BASE_BIT;

    // Count the number of trailing zeros in the mask
    trailing_zeros = 0;
    while (((mask >> trailing_zeros) & 1) == 0 && trailing_zeros < mask_bits) {
        trailing_zeros++;
    }

    // Calculate the range size
    return 1ULL << (trailing_zeros + MTRR_MASK_BASE_BIT);
}

static unsigned long compute_mtrr_physmask(unsigned long size, unsigned char maxphyaddr) {
    unsigned long temp_size, mask;
    unsigned char range_bits, mask_bits, zero_bits;

    // Ensure size is a power of two and at least 4KB
    if (size < (1ULL << MTRR_MASK_BASE_BIT) || (size & (size - 1)) != 0)
        return 0;

    // Calculate the number of address bits the size spans
    range_bits = 0;
    temp_size = size;
    while (temp_size > 1) {
        temp_size >>= 1;
        range_bits++;
    }

    // Determine the number of bits to set in the mask
    mask_bits = maxphyaddr - MTRR_MASK_BASE_BIT;
    mask = (1ULL << mask_bits) - 1;

    // Clear the bits corresponding to the range size
    zero_bits = range_bits - MTRR_MASK_BASE_BIT;
    mask &= ~((1ULL << zero_bits) - 1);

    printf("Phymask %lx\n", (mask << MTRR_MASK_BASE_BIT) | MTRR_PHYSMASK_VALID);

    // Shift mask to align with MTRR register and set valid bit
    return (mask << MTRR_MASK_BASE_BIT) | MTRR_PHYSMASK_VALID;
}

static int get_mtrr(unsigned int cpu, unsigned char id, unsigned long& base, unsigned long& size, unsigned char& memory_type,
                    unsigned char& active) {
    unsigned long base_msr = IA32_MTRR_PHYSBASE0 + (id * 2);
    unsigned long mask_msr = IA32_MTRR_PHYSMASK0 + (id * 2);
    unsigned long base_val, mask_val;

    if (read_msr(cpu, base_msr, &base_val) < 0 || read_msr(cpu, mask_msr, &mask_val) < 0)
        return -1;

    base = base_val & ~MTRR_MEMTYPE_MASK;
    memory_type = base_val & MTRR_MEMTYPE_MASK;
    active = (mask_val & MTRR_VALID_BIT) ? 1 : 0;
    size = compute_mtrr_range_size(mask_val, get_maxphysaddr());

    return 0;
}

static int set_mtrr(unsigned int cpu, unsigned char id, unsigned long base, unsigned long size, unsigned char memory_type,
                    unsigned char active) {
    unsigned long base_msr = IA32_MTRR_PHYSBASE0 + (id * 2);
    unsigned long mask_msr = IA32_MTRR_PHYSMASK0 + (id * 2);
    unsigned long base_val = (base & ~MTRR_MEMTYPE_MASK) | (memory_type & MTRR_MEMTYPE_MASK);
    unsigned long mask_val = 0x3fff00000000ul; //compute_mtrr_physmask(size, get_maxphysaddr());

    if (active)
        mask_val |= MTRR_VALID_BIT;

    if (write_msr(cpu, base_msr, base_val) < 0 || write_msr(cpu, mask_msr, mask_val) < 0)
        return -1;

    return 0;
}

static int clear_confilicting_mtrrs(unsigned long paddr, unsigned long size) {
    auto num_mtrrs = get_num_mtrrs();
    unsigned long base, mt_size;
    unsigned char memory_type, active;
    auto cpu = get_current_core_id();

    for (unsigned int i = 0; i < num_mtrrs; i++) {
        if (get_mtrr(cpu, i, base, mt_size, memory_type, active) < 0) {
            perror("get_mtrr");
            return -1;
        }
        // printf("MTRR %u - 0x%lx + 0x%lx\n", i, base, mt_size);
        if (base >= paddr + size)
            continue;
        if (base + mt_size <= paddr)
            continue;
        if (set_mtrr(cpu, i, 0, 0, 0, 0) >= 0) {
            printf("Cleared MTRR %u - Was 0x%lx + 0x%lx, conflicts with 0x%lx + 0x%lx\n", i, base, mt_size, paddr, size);
            continue;
        }
        perror("set_mtrr");
        printf("Could not clear MTRR %u\n", i);
        return -1;
    }

    return 0;
}

static unsigned char get_free_mtrr() {
    auto num_mtrrs = get_num_mtrrs();
    unsigned long base, mt_size;
    unsigned char memory_type, active;
    auto cpu = get_current_core_id();

    for (unsigned int i = 0; i < num_mtrrs; i++) {
        if (get_mtrr(cpu, i, base, mt_size, memory_type, active) < 0) {
            perror("get_mtrr");
            return ~0;
        }
        if ((!base && !mt_size) || !active)
            return i;
    }

    return ~0;
}

int main() {
    unsigned char core1, core2;
    const auto harness_size = reinterpret_cast<unsigned char*>(insn_harness_end) - reinterpret_cast<unsigned char*>(insn_harness);

    if (getuid() != 0) {
        puts("Give me root privileges!\n");
        exit(EXIT_FAILURE);
    }

    if (ptedit_init() < 0) {
        exit(EXIT_FAILURE);
    }

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

    // We optimistically assume that the test data regions are physically contiguous
    auto entry = ptedit_resolve(test_stack.raw, 0);
    auto harness_stack_paddr = (unsigned long) ptedit_cast(entry.pte, ptedit_pte_t).pfn << 12;
    entry = ptedit_resolve(test_data.raw, 0);
    auto harness_data_paddr = (unsigned long) ptedit_cast(entry.pte, ptedit_pte_t).pfn << 12;

    printf("Got physical addresses 0x%lx and 0x%lx\n", harness_stack_paddr, harness_data_paddr);

    get_sibling_cores(&core1, &core2);
    printf("Running on cores %u and %u\n", core1, core2);
    pin_to_core(core1);

    clear_confilicting_mtrrs(harness_stack_paddr, BACKUP_SIZE);
    clear_confilicting_mtrrs(harness_data_paddr, BACKUP_SIZE);
    auto target_mtrr = get_free_mtrr();
    if (target_mtrr == static_cast<unsigned char>(~0u)) {
        printf("All MTRRs are in use\n");
        exit(EXIT_FAILURE);
    }

    std::vector<msr_mask> msr_list {};
    unsigned int mtrr_msr = IA32_MTRR_PHYSBASE0 + 2*target_mtrr;
    msr_list.emplace_back(msr_mask {mtrr_msr, MTRR_MEMTYPE_MASK});
    mtrr_msr = IA32_MTRR_PHYSBASE0 + 2 * (target_mtrr + 1);
    msr_list.emplace_back(msr_mask {mtrr_msr, MTRR_MEMTYPE_MASK});

    set_mtrr(get_current_core_id(), target_mtrr, harness_stack_paddr, BACKUP_SIZE, MTYPE1, 1);
    set_mtrr(get_current_core_id(), target_mtrr + 1, harness_data_paddr, BACKUP_SIZE, MTYPE1, 1);

    while (1) {
        for (const auto test: test_registry()) {
            auto insn_test = test.first();

            if (!insn_test->is_okay()) {
                printf("skipping %s\n", test.second.c_str());
                delete insn_test;
                continue;
            }

            for (auto msr: msr_list) {
                const unsigned long mask = MTYPE1 ^ MTYPE2;
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