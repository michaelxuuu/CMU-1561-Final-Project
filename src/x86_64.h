#ifndef _x86_64_h_
#define _x86_64_h_

#include <stdint.h>

#ifdef __APPLE__
    #include <sys/ucontext.h>
    static inline void mc_set_rip(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rip = val; }
    static inline void mc_set_rsp(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rsp = val; }
    static inline void mc_set_rdi(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rdi = val; }
#else
    #include <ucontext.h>
    enum
    {
      REG_R8 = 0,
    # define REG_R8		REG_R8
      REG_R9,
    # define REG_R9		REG_R9
      REG_R10,
    # define REG_R10	REG_R10
      REG_R11,
    # define REG_R11	REG_R11
      REG_R12,
    # define REG_R12	REG_R12
      REG_R13,
    # define REG_R13	REG_R13
      REG_R14,
    # define REG_R14	REG_R14
      REG_R15,
    # define REG_R15	REG_R15
      REG_RDI,
    # define REG_RDI	REG_RDI
      REG_RSI,
    # define REG_RSI	REG_RSI
      REG_RBP,
    # define REG_RBP	REG_RBP
      REG_RBX,
    # define REG_RBX	REG_RBX
      REG_RDX,
    # define REG_RDX	REG_RDX
      REG_RAX,
    # define REG_RAX	REG_RAX
      REG_RCX,
    # define REG_RCX	REG_RCX
      REG_RSP,
    # define REG_RSP	REG_RSP
      REG_RIP,
    # define REG_RIP	REG_RIP
      REG_EFL,
    # define REG_EFL	REG_EFL
      REG_CSGSFS,		/* Actually short cs, gs, fs, __pad0.  */
    # define REG_CSGSFS	REG_CSGSFS
      REG_ERR,
    # define REG_ERR	REG_ERR
      REG_TRAPNO,
    # define REG_TRAPNO	REG_TRAPNO
      REG_OLDMASK,
    # define REG_OLDMASK	REG_OLDMASK
      REG_CR2
    # define REG_CR2	REG_CR2
    };
    static inline void mc_set_rip(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RIP] = val; }
    static inline void mc_set_rsp(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RSP] = val; }
    static inline void mc_set_rdi(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RIP] = val; }
#endif

#endif
