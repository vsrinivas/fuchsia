// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The format of data for r/w of x86_64 general regs.
// By convention this is MX_THREAD_STATE_REGSET0.

typedef struct mx_x86_64_general_regs {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
} mx_x86_64_general_regs_t;

// The format of data for r/w of arm64 general regs.
// By convention this is MX_THREAD_STATE_REGSET0.

typedef struct mx_arm64_general_regs {
    uint64_t r[30];
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
    uint64_t cpsr;
} mx_aarch64_general_regs_t;

#ifdef __cplusplus
}
#endif
