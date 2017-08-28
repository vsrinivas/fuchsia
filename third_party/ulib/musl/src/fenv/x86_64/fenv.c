// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fenv.h>

#include <magenta/compiler.h>
#include <stdint.h>
#include <x86intrin.h>

#define ROUND_MASK (FE_DOWNWARD | FE_UPWARD | FE_TOWARDZERO)
_Static_assert((ROUND_MASK << 3) == _MM_ROUND_MASK, "");

static inline uint16_t get_x87_sw(void) {
    uint16_t sw;
    __asm__("fnstsw %0" : "=a"(sw));
    return sw;
}

static inline uint16_t get_x87_cw(void) {
    uint16_t cw;
    __asm__("fnstcw %0" : "=m"(cw));
    return cw;
}

static inline void set_x87_cw(uint16_t cw) {
    __asm__("fldcw %0" :: "m"(cw));
}

int fegetround(void) {
    return _MM_GET_ROUNDING_MODE() >> 3;
}

__LOCAL int __fesetround(int round) {
    set_x87_cw((get_x87_cw() & ~ROUND_MASK) | round);
    _MM_SET_ROUNDING_MODE(round << 3);
    return 0;
}

int feclearexcept(int mask) {
    uint16_t sw = get_x87_sw();
    if (sw & mask & FE_ALL_EXCEPT)
        __asm__("fnclex");
    uint32_t mxcsr = _mm_getcsr();
    mxcsr |= sw & FE_ALL_EXCEPT;
    if (mxcsr & mask & FE_ALL_EXCEPT)
        _mm_setcsr(mxcsr & ~(mask & FE_ALL_EXCEPT));
    return 0;
}

int feraiseexcept(int mask) {
    _mm_setcsr(_mm_getcsr() | (mask & FE_ALL_EXCEPT));
    return 0;
}

int fetestexcept(int mask) {
    return (_mm_getcsr() | get_x87_sw()) & mask & FE_ALL_EXCEPT;
}

int fegetenv(fenv_t* env) {
    __asm__("fnstenv %0\n"
            "stmxcsr %1" : "=m"(*env), "=m"(env->__mxcsr));
    return 0;
}

static inline void install_fenv(const fenv_t* env) {
    __asm__("fldenv %0\n"
            "ldmxcsr %1"
            :: "m"(*env), "m"(env->__mxcsr));
}

int fesetenv(const fenv_t* env) {
    install_fenv(likely(env != FE_DFL_ENV) ? env :
                 &(fenv_t){
                     .__control_word = 0x37f,
                     .__tags = 0xffff,
                     .__mxcsr = 0x1f80,
                  });
    return 0;
}
