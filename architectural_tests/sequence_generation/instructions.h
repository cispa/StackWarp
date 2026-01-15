#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H
#ifdef __cplusplus

#include "ttoolbox.h"
#include <asmjit/asmjit.h>
#include <random>
#include <array>

#define USE_SSE 1
#define USE_SHA 1
#define USE_AVX 1
#define USE_VAES 1
#define USE_TSX 1
#define USE_XSAVE 1
#define NO_USE_MEM 0

// Only those for now
enum class OpType {
    IMM,
    GPQ,
    XMM,
    YMM,
    LABEL,
    MEM,
    KREG
};

constexpr static asmjit::x86::Gpq regs_gpq[] = {
        asmjit::x86::rax, asmjit::x86::rbx, asmjit::x86::rcx, asmjit::x86::rdx,
        asmjit::x86::rsp, asmjit::x86::rbp, asmjit::x86::rsi, /*asmjit::x86::rdi,*/
        asmjit::x86::r8, asmjit::x86::r9, asmjit::x86::r10, asmjit::x86::r11,
        asmjit::x86::r12, asmjit::x86::r13, asmjit::x86::r14, /*asmjit::x86::r15,*/
};

constexpr static asmjit::x86::Xmm regs_xmm[] = {
        asmjit::x86::xmm0, asmjit::x86::xmm1, asmjit::x86::xmm2, asmjit::x86::xmm3,
        asmjit::x86::xmm4, asmjit::x86::xmm5, asmjit::x86::xmm6, asmjit::x86::xmm7,
        asmjit::x86::xmm8, asmjit::x86::xmm9, asmjit::x86::xmm10, asmjit::x86::xmm11,
        asmjit::x86::xmm12, asmjit::x86::xmm13, asmjit::x86::xmm14, asmjit::x86::xmm15,
};

constexpr static asmjit::x86::Ymm regs_ymm[] = {
        asmjit::x86::ymm0, asmjit::x86::ymm1, asmjit::x86::ymm2, asmjit::x86::ymm3,
        asmjit::x86::ymm4, asmjit::x86::ymm5, asmjit::x86::ymm6, asmjit::x86::ymm7,
        asmjit::x86::ymm8, asmjit::x86::ymm9, asmjit::x86::ymm10, asmjit::x86::ymm11,
        asmjit::x86::ymm12, asmjit::x86::ymm13, asmjit::x86::ymm14, asmjit::x86::ymm15,
};

constexpr static asmjit::x86::Zmm regs_zmm[] = {
    asmjit::x86::zmm0, asmjit::x86::zmm1, asmjit::x86::zmm2, asmjit::x86::zmm3,
    asmjit::x86::zmm4, asmjit::x86::zmm5, asmjit::x86::zmm6, asmjit::x86::zmm7,
    asmjit::x86::zmm8, asmjit::x86::zmm9, asmjit::x86::zmm10, asmjit::x86::zmm11,
    asmjit::x86::zmm12, asmjit::x86::zmm13, asmjit::x86::zmm14, asmjit::x86::zmm15,
};

constexpr static asmjit::x86::KReg regs_kreg[] = {
    asmjit::x86::k0, asmjit::x86::k1, asmjit::x86::k2, asmjit::x86::k3, asmjit::x86::k4,
    asmjit::x86::k5, asmjit::x86::k6, asmjit::x86::k7
};

template<std::size_t... I>
constexpr std::array<asmjit::Imm, sizeof...(I)> make_array(std::index_sequence<I...>) {
    return {static_cast<unsigned int>(I)...};
}

constexpr auto imm_arr = make_array(std::make_index_sequence<256>{});

class Instruction {
protected:
    asmjit::Label start_label = asmjit::Label();
public:
    using FactoryFunc = Instruction *(*)();

    static inline std::vector<FactoryFunc> &registry() {
        static std::vector<FactoryFunc> factories;
        return factories;
    }

    static void registerClass(FactoryFunc func) {
        registry().push_back(func);
    }

    const static asmjit::x86::Gpq &get_random_Gp() {
        return regs_gpq[static_cast<unsigned int>(rand()) % countof(regs_gpq)];
    }

    const static asmjit::x86::Gpq &get_random_Gp_RAX() {
        return asmjit::x86::rax;
    }
    const static asmjit::x86::Gpq &get_random_Gp_RBX() {
        return asmjit::x86::rbx;
    }
    const static asmjit::x86::Gpq &get_random_Gp_RCX() {
        return asmjit::x86::rcx;
    }
    const static asmjit::x86::Gpq &get_random_Gp_RDX() {
        return asmjit::x86::rdx;
    }
    const static asmjit::x86::Gpq &get_random_DS_ZSI() {
        return asmjit::x86::rsi;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZDI() {
        return asmjit::x86::rdi;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZSI() {
        return asmjit::x86::rsi;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZAX() {
        return asmjit::x86::rax;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZBX() {
        return asmjit::x86::rbx;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZCX() {
        return asmjit::x86::rcx;
    }
    const static asmjit::x86::Gpq &get_random_Gp_ZDX() {
        return asmjit::x86::rdx;
    }
    const static asmjit::x86::Gpd &get_random_Gp_EAX() {
        return asmjit::x86::eax;
    }
    const static asmjit::x86::Gpd &get_random_Gp_EBX() {
        return asmjit::x86::ebx;
    }
    const static asmjit::x86::Gpd &get_random_Gp_ECX() {
        return asmjit::x86::ecx;
    }
    const static asmjit::x86::Gpd &get_random_Gp_EDX() {
        return asmjit::x86::edx;
    }
    const static asmjit::x86::GpbHi &get_random_Gp_AH() {
        return asmjit::x86::ah;
    }
    const static asmjit::x86::GpbLo &get_random_Gp_AL() {
        return asmjit::x86::al;
    }
    const static asmjit::x86::GpbLo &get_random_Gp_CL() {
        return asmjit::x86::cl;
    }
    const static asmjit::x86::Gpw &get_random_Gp_AX() {
        return asmjit::x86::ax;
    }
    const static asmjit::x86::Gpw &get_random_Gp_DX() {
        return asmjit::x86::ax;
    }

    const static asmjit::x86::Xmm &get_random_Xmm() {
        return regs_xmm[static_cast<unsigned int>(rand()) % countof(regs_xmm)];
    }

    const static asmjit::x86::Vec &get_random_Vec() {
        if (is_avx512_supported())
            return regs_zmm[static_cast<unsigned int>(rand()) % countof(regs_zmm)];
        return regs_ymm[static_cast<unsigned int>(rand()) % countof(regs_ymm)];
    }

    const static asmjit::Imm &get_random_Imm() {
        return imm_arr[static_cast<unsigned int>(rand()) % imm_arr.size()];
    }

    const static asmjit::x86::KReg &get_random_KReg() {
        return regs_kreg[static_cast<unsigned int>(rand()) % countof(regs_kreg)];
    }

    const static asmjit::x86::Mem get_random_Mem() {
        switch (static_cast<unsigned long>(rand()) % 2) {
            case 0:
                return asmjit::x86::Mem(asmjit::x86::rdi, 0);
            case 1:
                return asmjit::x86::Mem(asmjit::x86::rdi, (rand() % 0x1000) & ~0x3fu);
            /*case 2:
                return asmjit::x86::Mem(asmjit::x86::rdi, asmjit::x86::rsi, rand() % 4, 0);
            default:
                return asmjit::x86::Mem(asmjit::x86::rdi, asmjit::x86::rsi, rand() % 4, (rand() % 0x1000) & ~0x3fu);*/
        }
        __builtin_unreachable();
    }

    const asmjit::Label &get_random_Label() {
        return start_label;
    }

    virtual void emit(asmjit::x86::Assembler &a, const asmjit::Label &l) const = 0;

    virtual ~Instruction() = default;
};

#else
#error "You can only use this file with C++"
#endif
#endif