// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

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
} mx_arm64_general_regs_t;

// mx_thread_read_state, mx_thread_write_state
// The maximum size of thread state, in bytes, that can be processed by the
// read_state/write_state syscalls. It exists so code can expect a sane limit
// on the amount of memory needed to process the request.
#define MX_MAX_THREAD_STATE_SIZE 4096u

// The "general regs" are by convention in regset 0.
#define MX_THREAD_STATE_REGSET0 0u
#define MX_THREAD_STATE_REGSET1 1u
#define MX_THREAD_STATE_REGSET2 2u
#define MX_THREAD_STATE_REGSET3 3u
#define MX_THREAD_STATE_REGSET4 4u
#define MX_THREAD_STATE_REGSET5 5u
#define MX_THREAD_STATE_REGSET6 6u
#define MX_THREAD_STATE_REGSET7 7u
#define MX_THREAD_STATE_REGSET8 8u
#define MX_THREAD_STATE_REGSET9 9u

__END_CDECLS
