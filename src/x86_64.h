#ifndef _x86_64_h_
#define _x86_64_h_

#ifndef __APPLE__
    #define _GNU_SOURCE
#endif

#include <stdint.h>
#include <sys/ucontext.h>

#ifdef __APPLE__
    static inline void mc_set_rip(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rip = val; }
    static inline void mc_set_rsp(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rsp = val; }
    static inline void mc_set_rdi(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext->__ss.__rdi = val; }
#else
    static inline void mc_set_rip(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RIP] = val; }
    static inline void mc_set_rsp(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RSP] = val; }
    static inline void mc_set_rdi(mcontext_t uc_mcontext, uint64_t val) { uc_mcontext.gregs[REG_RIP] = val; }
#endif

#endif
