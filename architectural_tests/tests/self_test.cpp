#include "ttoolbox.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include "../harness.h"
#include "../common.h"

#define BLK "\e[0;30m"
#define RED "\e[0;31m"
#define GRN "\e[0;32m"
#define YEL "\e[0;33m"
#define BLU "\e[0;34m"
#define MAG "\e[0;35m"
#define CYN "\e[0;36m"
#define WHT "\e[0;37m"
#define CRESET "\e[0m"

asm (
    ".globl test_deterministic_1\n"
    "test_deterministic_1:\n"
    "aesenc %xmm0, %xmm1\n"
    "movq %xmm1, %r9\n"
    "push %r9\n"
    ".globl test_deterministic_1_end\n"
    "test_deterministic_1_end:\n"

    ".globl test_non_deterministic_1\n"
    "test_non_deterministic_1:\n"
    "rdtsc\n"
    ".globl test_non_deterministic_1_end\n"
    "test_non_deterministic_1_end:\n"

    "nop\n"
);

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

mk_test(deterministic_1)
mk_test(non_deterministic_1)
mk_test(deep_call)
mk_test(indirect1)
mk_test(direct1)
mk_test(stack1)
mk_test(rdrand1)
mk_test(rsb1)
mk_test(pht1)
mk_test(btb1)
mk_test(load_store_1)
mk_test(load_store_2)
mk_test(load_store_3)
mk_test(load_store_4)
mk_test(load_store_5)
mk_test(load_store_6)
mk_test(self_modifying_1)
mk_test(self_modifying_2)
mk_test(evict_1)
mk_test(evict_2)
mk_test(evict_3) /// test_string_set_1
mk_test(string_set_1)
mk_test(string_move_1)

int main() {
    const auto harness_size = reinterpret_cast<unsigned char*>(insn_harness_end) - reinterpret_cast<unsigned char*>(insn_harness);

    // Make harness RWX so that we can JIT without worries
    if (mprotect(reinterpret_cast<void*>(insn_harness), page_align(harness_size), PROT_READ | PROT_WRITE | PROT_EXEC)) {
        perror("mprotect");
        exit(EXIT_FAILURE);
    }

    printf("We expect all tests except for 'non_deterministic_1' to be deterministic.\n\n");

    for (const auto& test: test_registry()) {
        auto instance = test.first();
        auto is_ok = instance->is_okay();
        printf("%22s: %20s (checksum %08x)\n", test.second.c_str(), is_ok ? GRN "Deterministic" CRESET : RED "Non-Deterministic" CRESET, instance->last_checksum());
        delete instance;
    }

    return 0;
}