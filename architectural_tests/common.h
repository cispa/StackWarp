#ifndef TESTS_COMMON___H
#define TESTS_COMMON___H
#ifdef __cplusplus

#include "instructions.h"
#include "msr_flipper.h"

int init_msrs();
void init_regs_generic();
std::vector<Instruction*(*)()> check_all_instructions();
std::string disassemble(Instruction* (*gen)());

enum class InsnSequenceTestResult {
    SeqTestCrash = 0, // Both crash, no difference in register state at crash time
    SeqTestOK,  // None crash, no difference in register state
    SeqTestEnableDisable, // One crashes while the other does not
    SeqTestBehaviorChange, // None crash or both crash, but there is a difference in the register state. This is the one we are looking for
};

class InsnSequenceTest {
protected:
    unsigned char sequence[MAX_SEQUENCE_SIZE];
    size_t sequence_len;
    unsigned int last_signal;
    unsigned int checksum;

    void self_emit(const std::vector<Instruction*>& insns);
    int create_report(const char* dest, int last_signal, const msr_flipper_arg& msr_arg) const;
    InsnSequenceTestResult eval_test(const msr_flipper_arg& arg, unsigned int signal, unsigned int new_checksum);
    unsigned int get_checksum();

    explicit InsnSequenceTest() : sequence{}, sequence_len(0), last_signal(0), checksum(0) {}

public:
    bool is_okay();
    unsigned int last_checksum() const {return checksum;}
    unsigned int get_last_signal() const {return last_signal;}

    // Test the instruction sequence and make a report if we find architectural behavior changes
    InsnSequenceTestResult run_test_simple_flip(const msr_flipper_arg& arg);

    InsnSequenceTestResult run_test_flip_timed(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_buggy_flip(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_perma_set(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_jitter(const msr_flipper_arg& arg);

    InsnSequenceTestResult run_test_jitter_slow(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_jitter_all_set(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_jitter_zero(const msr_flipper_arg &arg);

    InsnSequenceTestResult run_test_jitter_all(const msr_flipper_arg &arg);

    explicit InsnSequenceTest(const std::vector<Instruction*>& insns) : sequence{}, sequence_len(0), last_signal(0), checksum(0) {
        self_emit(insns);
        checksum = get_checksum();
    }

    explicit InsnSequenceTest(const std::vector<Instruction*(*)()>& insn_generators) : sequence{}, sequence_len(0), last_signal(0), checksum(0)  {
        std::vector<Instruction*> insns;
        for (const auto& generator: insn_generators)
            insns.push_back(generator());
        self_emit(insns);
        for (const auto insn: insns)
            delete insn;
        checksum = get_checksum();
    }

    explicit InsnSequenceTest(const void* code, unsigned long size) : sequence{}, sequence_len(size), last_signal(0), checksum(0) {
        memcpy(sequence, code, std::min(sequence_len, sizeof(sequence)));
        checksum = get_checksum();
    }

    ~InsnSequenceTest() = default;
};

class MonitoringInsnSequenceTest : public InsnSequenceTest {
    void monitoring_self_emit(const std::vector<Instruction*>& insns);
public:
    explicit MonitoringInsnSequenceTest(const std::vector<Instruction*>& insns) {
        monitoring_self_emit(insns);
        checksum = get_checksum();
    }
};

extern "C" {
    struct insn_report {
        union register_backup state_pre;
        union {
            union register_backup state_post;
            struct ucontext_t uc_post;
        };
        unsigned char stack [BACKUP_SIZE];
        unsigned char data [BACKUP_SIZE];
        unsigned char sequence [MAX_SEQUENCE_SIZE];
        unsigned int signal;
        msr_flipper_arg msr;
    } __attribute__((packed));
}
#endif
#endif