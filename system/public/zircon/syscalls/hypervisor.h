// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <zircon/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

enum {
    ZX_GUEST_TRAP_BELL  = 0,
    ZX_GUEST_TRAP_MEM   = 1,
    ZX_GUEST_TRAP_IO    = 2,
};

// Structure to create a VCPU for a guest.
typedef struct zx_vcpu_create_args {
    zx_vaddr_t ip;
#if __x86_64__
    zx_vaddr_t cr3;
    zx_handle_t apic_vmo;
#endif // __x86_64__
} zx_vcpu_create_args_t;

enum {
    ZX_VCPU_STATE   = 0,
    ZX_VCPU_IO      = 1,
};

// Structure to read and write VCPU state.
typedef struct zx_vcpu_state {
#if __aarch64__
    uint64_t r[31];
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
    // Only the user-controllable lower 32-bits of the flags register.
    uint32_t flags;
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
