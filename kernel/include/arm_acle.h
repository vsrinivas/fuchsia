// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// Include arm_acle.h from the toolchain headers.
#include_next <arm_acle.h>

#include <stdint.h>

#ifndef __clang__

// GCC's arm_acle.h is missing implementations of the following ARM-standard APIs.
// Thus they are provided here.

#define __yield() __asm__ volatile("yield" ::: "memory")
#define __dsb(mb) __asm__ volatile("dsb %0" :: "i"(mb) : "memory")
#define __dmb(mb) __asm__ volatile("dmb %0" :: "i"(mb) : "memory")
#define __isb(mb) __asm__ volatile("isb %0" :: "i"(mb) : "memory")

#define __arm_rsr64(reg) \
    ({                                                \
        uint64_t _val;                                \
        __asm__ volatile("mrs %0," reg : "=r"(_val)); \
        _val;                                         \
    })

#define __arm_rsr(reg) \
    ({                                                \
        uint32_t _val;                                \
        __asm__ volatile("mrs %0," reg : "=r"(_val)); \
        _val;                                         \
    })

#define __arm_wsr64(reg, val) \
    ({                                                    \
        uint64_t _val = (val);                            \
        __asm__ volatile("msr " reg ", %0" :: "r"(_val)); \
    })

#define __arm_wsr(reg, val) \
    ({                                                    \
        uint32_t _val = (val);                            \
        __asm__ volatile("msr " reg ", %0" :: "r"(_val)); \
    })

#endif // !__clang__
