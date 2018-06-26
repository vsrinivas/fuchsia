// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

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

// Value for ZX_THREAD_STATE_FP_REGS on x64. Holds x87 and MMX state.
typedef struct zx_thread_state_fp_regs {
    uint16_t fcw; // Control word.
    uint16_t fsw; // Status word.
    uint8_t ftw;  // Tag word.
    uint8_t reserved;
    uint16_t fop; // Opcode.
    uint64_t fip; // Instruction pointer.
    uint64_t fdp; // Data pointer.

    // The x87/MMX state. For x87 the each "st" entry has the low 80 bits used for the register
    // contents. For MMX, the low 64 bits are used. The higher bits are unused.
    __ALIGNED(16)
    struct {
        uint64_t low;
        uint64_t high;
    } st[8];
} zx_thread_state_fp_regs_t;

// Value for ZX_THREAD_STATE_VECTOR_REGS on x64. Holds SSE and AVX registers.
//
// Setting vector registers will only work for threads that have previously executed an
// instruction using the corresponding register class.
typedef struct zx_thread_state_vector_regs {
    // When only 16 registers are supported (pre-AVX-512), zmm[16-31] will be 0. YMM registers (256
    // bits) are v[0-4], XMM registers (128 bits) are v[0-2].
    struct {
        uint64_t v[8];
    } zmm[32];

    // AVX-512 opmask registers. Will be 0 unless AVX-512 is supported.
    uint64_t opmask[8];

    // SIMD control and status register.
    uint32_t mxcsr;
} zx_thread_state_vector_regs_t;

// Value for ZX_THREAD_STATE_EXTRA_REGS on x64.
// TODO(brettw) reading and writing this is currently unimplemented.
typedef struct zx_thread_state_extra_regs {
    uint64_t fs;
    uint64_t gs;
} zx_thread_state_extra_regs_t;

#elif defined(__aarch64__)

// Value for ZX_THREAD_STATE_GENERAL_REGS on ARM64 platforms.
typedef struct zx_thread_state_general_regs {
    uint64_t r[30];
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
    uint64_t cpsr;
} zx_thread_state_general_regs_t;

// Value for ZX_THREAD_STATE_FP_REGS on ARM64 platforms. This is unused because vector state is
// used for all floating point on ARM64.
typedef struct zx_thread_state_fp_regs {
    // Avoids sizing differences for empty structs between C and C++.
    uint32_t unused;
} zx_thread_state_fp_regs_t;

// Value for ZX_THREAD_STATE_VECTOR_REGS on ARM64 platforms.
typedef struct zx_thread_state_vector_regs {
    uint32_t fpcr;
    uint32_t fpsr;
    struct {
        uint64_t low;
        uint64_t high;
    } v[32];
} zx_thread_state_vector_regs_t;

// Value for ZX_THREAD_STATE_EXTRA_REGS on ARM64 platforms. Currently unused.
typedef struct zx_thread_state_extra_regs {
    // Avoids sizing differences for empty structs between C and C++.
    uint32_t unused;
} zx_thread_state_extra_regs_t;

#endif

// Value for ZX_THREAD_STATE_SINGLE_STEP. The value can be 0 (not single-stepping), or 1
// (single-stepping). Other values will give ZX_ERR_INVALID_ARGS.
typedef uint32_t zx_thread_state_single_step_t;

// Possible values for "kind" in zx_thread_read_state and zx_thread_write_state.
typedef enum {
    ZX_THREAD_STATE_GENERAL_REGS = 0, // zx_thread_state_general_regs_t value.
    ZX_THREAD_STATE_FP_REGS = 1,      // zx_thread_state_fp_regs_t value.
    ZX_THREAD_STATE_VECTOR_REGS = 2,  // zx_thread_state_vector_regs_t value.
    ZX_THREAD_STATE_EXTRA_REGS = 3,   // zx_thread_state_extra_regs_t value.
    ZX_THREAD_STATE_SINGLE_STEP = 4   // zx_thread_state_single_step_t value.
} zx_thread_state_topic_t;

__END_CDECLS
