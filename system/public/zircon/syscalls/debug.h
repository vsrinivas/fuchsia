// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

#if defined(__x86_64__)

// Value for ZX_THREAD_STATE_GENERAL_REGS on x86-64 platforms.
typedef struct zx_thread_state_general_regs {
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
} zx_thread_state_general_regs_t;

#elif defined(__aarch64__)

// Value for ZX_THREAD_STATE_GENERAL_REGS on ARM64 platforms.
typedef struct zx_thread_state_general_regs {
    uint64_t r[30];
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
    uint64_t cpsr;
} zx_thread_state_general_regs_t;

#endif

// Value for ZX_THREAD_STATE_SINGLE_STEP. The value can be 0 (not single-stepping), or 1
// (single-stepping). Other values will give ZX_ERR_INVALID_ARGS.
typedef uint32_t zx_thread_state_single_step_t;

// Possible values for "kind" in zx_thread_read_state and zx_thread_write_state.
typedef enum {
    ZX_THREAD_STATE_GENERAL_REGS = 0, // zx_thread_state_general_regs_t value.
    ZX_THREAD_STATE_SINGLE_STEP = 1   // zx_thread_state_single_step_t value.
} zx_thread_state_topic_t;

__END_CDECLS
