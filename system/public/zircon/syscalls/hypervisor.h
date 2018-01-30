// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
enum {
    ZX_GUEST_TRAP_BELL  = 0,
    ZX_GUEST_TRAP_MEM   = 1,
    ZX_GUEST_TRAP_IO    = 2,
};

enum {
    ZX_VCPU_STATE       = 0,
    ZX_VCPU_IO          = 1,
};
// clang-format on

// Structure to read and write VCPU state.
typedef struct zx_vcpu_state {
#if __aarch64__
    uint64_t x[31];
    uint64_t sp;
    // Contains only the user-controllable upper 4-bits (NZCV).
    uint32_t cpsr;
#elif __x86_64__
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    // Contains only the user-controllable lower 32-bits.
    uint64_t rflags;
#endif
} zx_vcpu_state_t;

// Structure to read and write VCPU state for IO ports.
typedef struct zx_vcpu_io {
    uint8_t access_size;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint8_t data[4];
    };
} zx_vcpu_io_t;

__END_CDECLS
