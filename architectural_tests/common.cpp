#include "ttoolbox.h"
#include <asmjit/asmjit.h>
#include <unistd.h>
#include <wait.h>
#include <libgen.h>
#include <zstd.h>
#include "instructions.h"
#include "harness.h"
#include "common.h"
#include <memory>
#include <smmintrin.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "msr_flipper.h"
#include "msr_lists.h"

// For now, we only look at instructions that do not produce faults under normal user-mode conditions
#define NON_FAULTING_ONLY 1

using namespace asmjit;

extern "C" void init_state_xsave();

static unsigned char *shared_page = nullptr;
struct loc_r {
    unsigned int checksum;
    unsigned int signal;
} volatile* shared_results;
static int msr_flipper_fd = -1;
static unsigned char __attribute__((aligned(0x1000))) sync_page_[0x1000];
volatile unsigned char* sync_page = sync_page_;

std::string disassemble(Instruction* (*gen)()) {
    auto insn = gen();
    char pbuf[128] = {0, };
    char* ppbuf = pbuf;

    CodeHolder code;
    JitRuntime rt;

    code.init(rt.environment(), rt.cpuFeatures());
    StringLogger logger;
    code.setLogger(&logger);

    x86::Assembler a(&code);

    auto loop = a.newLabel();
    insn->emit(a, loop);

    void (*fn)();
    rt.add(&fn, &code);

    strncpy(pbuf, logger.data() , sizeof(pbuf) - 1);

    return strsep(&ppbuf, "\n");
}

static void crc32_append(unsigned int& checksum, const unsigned char* buf, unsigned long size) {
    unsigned long i;

    for (i = 0; i < size; i++) {
        checksum = _mm_crc32_u8(checksum, buf[i]);
    }
}

void init_regs_generic() {
    unsigned int i;
    auto dest_addr = reinterpret_cast<unsigned long>(&test_data);

    for (i = 0; i < (128 / sizeof(unsigned long)); i++)
        (reinterpret_cast<unsigned long*>(harness_init_reg_state.raw))[i] = dest_addr + i;
    init_state_xsave();
}

void init_regs(const register_backup& state, bool do_generic_xsave) {
    memcpy(harness_init_reg_state.raw, state.raw, sizeof(state));
    if (do_generic_xsave)
        init_state_xsave();
}

extern "C" void harness_fault_handler(int signum, siginfo_t *info, void *context) {
    sync_page[1] = 0;
    memcpy(shared_page, context, std::min<size_t>(sizeof(ucontext_t), BACKUP_SIZE));

    //printf("\n\n\nCrashed! %d, %llx\n", signum, ((ucontext_t*)context)->uc_mcontext.gregs[REG_RIP]);
    //fflush(stdout);

    signal(signum, SIG_DFL);
    raise(signum);
}

// Check whether the instruction sequence works and produces deterministic output
static void execute_in_harness(const void *fn, unsigned long size, unsigned int& signal, unsigned int& checksum, volatile unsigned char* start_signal, unsigned char core) {
    int status = 0;

    signal = 0;
    checksum = 0;

    if (!shared_page) {
        shared_page = static_cast<unsigned char *>
            (mmap(NULL, page_align(3*BACKUP_SIZE + sizeof(loc_r)), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED | MAP_POPULATE, -1, 0));

        if (shared_page == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }
        shared_results = reinterpret_cast<loc_r *>(shared_page + 3*BACKUP_SIZE);
        init_regs_generic();
    }

    shared_results->checksum = 0;
    shared_results->signal = 0;

    if (size > MAX_SEQUENCE_SIZE)
        return;

    auto child_pid = fork();
    if (!child_pid) {
        struct sigaction sa_segv;

        die_with_parent();
        pin_to_core(core);

        memset(&sa_segv, 0, sizeof(sa_segv));
        sa_segv.sa_sigaction = harness_fault_handler;
        sa_segv.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        sigemptyset(&sa_segv.sa_mask);

        sigaction(SIGSEGV, &sa_segv, nullptr);
        sigaction(SIGILL, &sa_segv, nullptr);
        sigaction(SIGTRAP, &sa_segv, nullptr);
        sigaction(SIGFPE, &sa_segv, nullptr);
        sigaction(SIGTRAP, &sa_segv, nullptr);
        sigaction(SIGBUS, &sa_segv, nullptr);

        memcpy(test_sequence, fn, size);
        sched_yield();
        //ualarm(500000, 0);
        alarm(60);

        // Init
        sync_page[1] = 0;
        // Tell kernel module that we are here
        sync_page[2] = 1;
        if (start_signal)
            while (!*start_signal) {}
        if (sync_page[3]) {
            *sync_page = 0;
            raise(SIGUSR1);
        }

        shared_results->checksum = insn_harness();
        memcpy(shared_page, harness_result_reg_state.raw, BACKUP_SIZE);
        memcpy(shared_page + BACKUP_SIZE, &test_stack, BACKUP_SIZE);
        memcpy(shared_page + 2*BACKUP_SIZE, &test_data, BACKUP_SIZE);
        shared_results->signal = SIGUSR2;
        raise(SIGUSR2);
        __builtin_unreachable();
    }

    waitpid(child_pid, &status, 0);
    sync_page[1] = 0;
    sync_page[3] = 0;
    if (WIFSIGNALED(status))
        signal = WTERMSIG(status);

    //printf( " ---%d---", signal);
    //fflush(stdout);

    memcpy(&harness_result_reg_state, shared_page, BACKUP_SIZE);
    memcpy(&test_stack, shared_page + BACKUP_SIZE, BACKUP_SIZE);
    memcpy(&test_data, shared_page + 2*BACKUP_SIZE, BACKUP_SIZE);
    checksum = shared_results->checksum;
}

// Check whether the instruction produces deterministic output or at least crashes predictably
static bool is_harness_okay(const void *fn, unsigned long size) {
    unsigned int signal_1 = 0, signal_2 = 0, checksum_1 = 0, checksum_2 = 0;

    execute_in_harness(fn, size, signal_1, checksum_1, nullptr, get_current_core_id());
#ifdef NON_FAULTING_ONLY
    if (signal_1 != SIGUSR2)
        return false;
#endif
    execute_in_harness(fn, size, signal_2, checksum_2, nullptr, get_current_core_id());
    if (signal_1 != signal_2 || checksum_1 != checksum_2)
        return false;

    return true;
}

int InsnSequenceTest::create_report(const char* dest, int last_signal, const msr_flipper_arg& msr_arg) const {
    auto report = std::make_shared<struct insn_report>();

    // Fill report struct
    memcpy(report->state_pre.raw, harness_init_reg_state.raw, sizeof(harness_init_reg_state.raw));
    memcpy(report->state_post.raw, harness_result_reg_state.raw, sizeof(harness_result_reg_state.raw));
    memcpy(report->stack, &test_stack, sizeof(harness_result_reg_state.raw));
    memcpy(report->data, &test_data, sizeof(harness_result_reg_state.raw));
    memcpy(report->sequence, sequence, std::min(sizeof(sequence), sizeof(report->sequence)));
    report->signal = last_signal;
    report->msr = msr_arg;

    // Compress struct
    auto cctx = ZSTD_createCCtx();
    if (!cctx)
        return -1;
    auto buffOutSize = ZSTD_compressBound(sizeof(*report));
    std::shared_ptr<unsigned char[]> buffOut(new unsigned char[buffOutSize]);
    if (!buffOut) {
        ZSTD_freeCCtx(cctx);
        return -1;
    }
    const auto cSize = ZSTD_compressCCtx(cctx, buffOut.get(), buffOutSize, report.get(), sizeof(*report), 3);
    ZSTD_freeCCtx(cctx);

    if (ZSTD_isError(cSize))
        return -1;

    // Create the destination directory if it does not already exist
    const auto path_copy = new char[strlen(dest) + 1];
    strcpy(path_copy, dest);
    auto dir = dirname(path_copy);
    const auto rc = create_directory_recursively(dir);
    delete [] path_copy;
    if (rc)
        return rc;

    // Save
    const auto f = fopen(dest, "wb");
    if (!f)
        return -1;
    fwrite(buffOut.get(), 1, cSize, f);
    fclose(f);

    return 0;
}

bool InsnSequenceTest::is_okay() {
    unsigned int old_signal = last_signal;

#ifdef NON_FAULTING_ONLY
    if (old_signal != SIGUSR2)
        return false;
#endif

    auto new_checksum = get_checksum();

    if (last_signal != old_signal || new_checksum != checksum)
        return false;

    return true;
}

unsigned int InsnSequenceTest::get_checksum() {
    unsigned int signal = 0, checksum = 0;

    sync_page[1] = 0;
    execute_in_harness(sequence, sequence_len, signal, checksum, nullptr, get_current_core_id());

    last_signal = signal;
    return checksum;
}

void InsnSequenceTest::self_emit(const std::vector<Instruction*>& insns) {
    CodeHolder code;
    JitRuntime rt;

    code.init(rt.environment(), rt.cpuFeatures());
    StringLogger logger;
    code.setLogger(&logger);

    x86::Assembler a(&code);

    auto loop = a.newLabel();
    for (const auto insn: insns)
        insn->emit(a, loop);

    a.bind(loop);

    void (*fn)() = nullptr;
    rt.add(&fn, &code);

    if (!fn) {
        // If we cannot emit the code for any reason, just write ud2
        unsigned char ud2[] = {0x0f, 0x0b};
        memcpy(sequence, ud2, sizeof(ud2));
        return;
    }

    // printf("--%lx-- ", code.codeSize());

    sequence_len = std::min(sizeof(sequence), code.codeSize());
    memcpy(sequence, reinterpret_cast<void*>(fn), std::min(sizeof(sequence), sequence_len));
}

void MonitoringInsnSequenceTest::monitoring_self_emit(const std::vector<Instruction*>& insns) {
    CodeHolder code;
    JitRuntime rt;

    code.init(rt.environment(), rt.cpuFeatures());
    StringLogger logger;
    code.setLogger(&logger);

    x86::Assembler a(&code);

    auto loop = a.newLabel();
    a.xor_(x86::r15, x86::r15);
    for (const auto insn: insns) {
        insn->emit(a, loop);
        a.mov(x86::rsi, imm(reinterpret_cast<uintptr_t>(&insn_harness_accumulate_state)));
        a.call(x86::rsi);
        a.crc32(x86::r15d, x86::rax);
    }
    a.mov(x86::rax, x86::r15);

    a.bind(loop);

    void (*fn)() = nullptr;
    rt.add(&fn, &code);

    printf("--%lx-- ", code.codeSize());

    if (!fn) {
        // If we cannot emit the code for any reason, just write ud2
        unsigned char ud2[] = {0x0f, 0x0b};
        sequence_len = sizeof(ud2);
        memcpy(sequence, ud2, sizeof(ud2));
        return;
    }

    sequence_len = std::min(sizeof(sequence), code.codeSize());
    memcpy(sequence, reinterpret_cast<void*>(fn), std::min(sizeof(sequence), sequence_len));
}

std::vector<Instruction*(*)()> check_all_instructions() {
    unsigned int i = 0;
    std::vector<Instruction*(*)()> ret;

    for (const auto factory: Instruction::registry()) {
        printf("\rChecking instruction determinism... %03u/%03lu (%03lu correctly emitted and deterministic)", ++i, Instruction::registry().size(), ret.size());
        fflush(stdout);

        CodeHolder code;
        JitRuntime rt;

        code.init(rt.environment(), rt.cpuFeatures());
        StringLogger logger;
        code.setLogger(&logger);

        x86::Assembler a(&code);

        auto loop = a.newLabel();
        const auto* insn = factory();
        insn->emit(a, loop);
        a.bind(loop);
        delete insn;

        const void* fn;
        if (auto err = rt.add(&fn, &code))
            continue;

        auto codeSize = code.codeSize();

        if (auto ok = is_harness_okay(fn, codeSize))
            ret.push_back(factory);
        //else
        //    printf("\n%s\n", logger.data());

    }

    return ret;
}

InsnSequenceTestResult InsnSequenceTest::eval_test(const msr_flipper_arg& arg, unsigned int signal, unsigned int new_checksum) {
    const auto oldsig = last_signal;
    last_signal = signal;

    // printf("\nsig %u, %u / sum %x - %x\n", signal, oldsig, new_checksum, checksum);

    if (signal != oldsig && signal != SIGUSR2)
        return InsnSequenceTestResult::SeqTestEnableDisable;

    if (signal != SIGUSR2)
        return InsnSequenceTestResult::SeqTestCrash;

    if (checksum != new_checksum) {
        unsigned int report_checksum = 0;
        crc32_append(report_checksum, sequence, sequence_len);
        crc32_append(report_checksum, reinterpret_cast<const uint8_t *>(&arg), sizeof(arg));
        crc32_append(report_checksum, reinterpret_cast<const uint8_t *>(&new_checksum), sizeof(new_checksum));

        char dest_path[256];
        snprintf(dest_path, sizeof(dest_path), "reports/msr%lx_mask%lx_%08x.bin", arg.id, arg.mask, report_checksum);
        (void) create_report(dest_path, static_cast<int>(signal), arg);

        return InsnSequenceTestResult::SeqTestBehaviorChange;
    }

    return InsnSequenceTestResult::SeqTestOK;
}

InsnSequenceTestResult InsnSequenceTest::run_test_simple_flip(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg;
    unsigned int signal = 0, new_checksum = 0;

    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_FLIP, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, nullptr, get_current_core_id());

    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_FLIP, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    return eval_test(arg, signal, new_checksum);
}

InsnSequenceTestResult InsnSequenceTest::run_test_flip_timed(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg;
    unsigned int signal = 0, new_checksum = 0;

    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_FLIP_SINGLE, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, nullptr, get_current_core_id());

    return eval_test(arg, signal, new_checksum);
}

InsnSequenceTestResult InsnSequenceTest::run_test_jitter(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg;
    unsigned int signal = 0, new_checksum = 0;

    for (const auto& bl: jitter_blacklist) {
        if (bl.msr == arg.id && (bl.mask & arg.mask) != 0)
            return InsnSequenceTestResult::SeqTestOK;
    }

    sync_page[1] = 0;
    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_JITTER_AWAIT, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, NULL, get_current_core_id());
    while (*sync_page)
        sched_yield();

    return eval_test(arg, signal, new_checksum);
}

InsnSequenceTestResult InsnSequenceTest::run_test_jitter_slow(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg;
    unsigned int signal = 0, new_checksum = 0;

    for (const auto& bl: jitter_blacklist) {
        if (bl.msr == arg.id && (bl.mask & arg.mask) != 0)
            return InsnSequenceTestResult::SeqTestOK;
    }

    sync_page[1] = 0;
    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_JITTER_AWAIT_SLOW, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP_SLOW");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, sync_page, get_current_core_id());
    while (*sync_page)
        sched_yield();

    return eval_test(arg, signal, new_checksum);
}

InsnSequenceTestResult InsnSequenceTest::run_test_jitter_all_set(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg;
    unsigned int signal = 0, new_checksum = 0;
    unsigned long val_dest = 0;

    for (const auto& bl: jitter_blacklist) {
        if (bl.msr == arg.id && (bl.mask & arg.mask) != 0)
            return InsnSequenceTestResult::SeqTestOK;
    }

    read_msr(arg.core, arg.id, &val_dest);
    arg_wr.mask = val_dest;

    sync_page[1] = 0;
    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_JITTER_AWAIT, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, sync_page, get_current_core_id());
    while (*sync_page)
        sched_yield();

    return eval_test(arg, signal, new_checksum);
}

InsnSequenceTestResult InsnSequenceTest::run_test_jitter_zero(const msr_flipper_arg& arg) {
    msr_flipper_arg arg_wr = arg, arg_set = {arg.id, 0, 0, 0, arg.core};
    unsigned int signal = 0, new_checksum = 0;
    unsigned long orig_val;

    for (const auto& bl: jitter_blacklist) {
        if (bl.msr == arg.id)
            return InsnSequenceTestResult::SeqTestOK;
    }

    read_msr(arg.core, arg.id, &orig_val);

    ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_WR, reinterpret_cast<unsigned long>(&arg_set));
    arg_set.value = orig_val;

    sync_page[1] = 0;
    if (ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_JITTER_AWAIT, reinterpret_cast<unsigned long>(&arg_wr)) < 0) {
        perror("IOCTL_MSR_FLIPPER_FLIP");
        ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_WR, reinterpret_cast<unsigned long>(&arg_set));
        return InsnSequenceTestResult::SeqTestCrash;
    }

    execute_in_harness(sequence, sequence_len, signal, new_checksum, sync_page, get_current_core_id());
    while (*sync_page)
        sched_yield();

    ioctl(msr_flipper_fd, IOCTL_MSR_FLIPPER_WR, reinterpret_cast<unsigned long>(&arg_set));

    return eval_test(arg, signal, new_checksum);
}

int init_msrs() {
    msr_flipper_fd = open("/dev/" MSR_FLIPPER_FILE_NAME, O_RDWR);
    if (msr_flipper_fd < 0) {
        perror("open /dev/" MSR_FLIPPER_FILE_NAME);
        return msr_flipper_fd;
    }

    sync_page = static_cast<unsigned char*>(mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, msr_flipper_fd, 0));
    if (!sync_page) {
        perror("mmap /dev/" MSR_FLIPPER_FILE_NAME);
        return -1;
    }

    sync_page[2] = 1;
    sync_page[2] = 0;

    return 0;
}