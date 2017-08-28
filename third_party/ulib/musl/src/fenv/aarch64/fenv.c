// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fenv.h>

#include <magenta/compiler.h>
#include <stdint.h>

#define ROUND_MASK (FE_DOWNWARD | FE_UPWARD | FE_TOWARDZERO)

static inline uint32_t get_fpcr(void) {
    uint64_t value;
    __asm__("mrs %0, fpcr" : "=r"(value));
    return value;
}

static inline void set_fpcr(uint32_t value) {
    __asm__("msr fpcr, %0" :: "r"((uint64_t)value));
}

static inline uint32_t get_fpsr(void) {
    uint64_t value;
    __asm__("mrs %0, fpsr" : "=r"(value));
    return value;
}

static inline void set_fpsr(uint32_t value) {
    __asm__("msr fpsr, %0" :: "r"((uint64_t)value));
}

int fegetround(void) {
    return get_fpcr() & ROUND_MASK;
}

__LOCAL int __fesetround(int round) {
    uint64_t fpcr = get_fpcr();
    set_fpcr((fpcr & ~ROUND_MASK) | round);
    return 0;
}

int feclearexcept(int mask) {
    set_fpsr(get_fpsr() & ~(mask & FE_ALL_EXCEPT));
    return 0;
}

int feraiseexcept(int mask) {
    set_fpsr(get_fpsr() | (mask & FE_ALL_EXCEPT));
    return 0;
}

int fetestexcept(int mask) {
    return get_fpsr() & mask & FE_ALL_EXCEPT;
}

int fegetenv(fenv_t* env) {
    *env = (fenv_t){.__fpcr = get_fpcr(), .__fpsr = get_fpsr()};
    return 0;
}

int fesetenv(const fenv_t* env) {
    uint32_t fpcr = 0, fpsr = 0;
    if (likely(env != FE_DFL_ENV)) {
        fpcr = env->__fpcr;
        fpsr = env->__fpsr;
    }
    set_fpcr(fpcr);
    set_fpsr(fpsr);
    return 0;
}
