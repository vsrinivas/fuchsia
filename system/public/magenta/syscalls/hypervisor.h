// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

#define MX_GUEST_MAX_PKT_SIZE   32u
// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
#define X86_MAX_INST_LEN        15u

enum {
    MX_GUEST_TRAP_MEMORY = 1,
    MX_GUEST_TRAP_IO     = 2,
};

typedef struct mx_guest_io {
    uint16_t port;
    uint8_t access_size;
    bool input;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint8_t data[4];
    };
} mx_guest_io_t;

typedef struct mx_guest_memory {
    mx_vaddr_t addr;
#if __aarch64__
    uint32_t inst;
#elif __x86_64__
    uint8_t inst_len;
    uint8_t inst_buf[X86_MAX_INST_LEN];
#endif
} mx_guest_memory_t;

enum {
    MX_GUEST_PKT_MEMORY = 1,
    MX_GUEST_PKT_IO     = 2,
};

// Structure for processing guest state.
typedef struct mx_guest_packet {
    uint8_t type;
    union {
        // MX_GUEST_PKT_MEMORY
        mx_guest_memory_t memory;
        // MX_GUEST_PKT_IO
        mx_guest_io_t io;
    };
} mx_guest_packet_t;

static_assert(sizeof(mx_guest_packet_t) <= MX_GUEST_MAX_PKT_SIZE,
              "size of mx_guest_packet_t must not exceed "
              "MX_GUEST_MAX_PKT_SIZE");

// Structure to create a VCPU for a guest.
typedef struct mx_vcpu_create_args {
    mx_vaddr_t ip;
#if __x86_64__
    mx_vaddr_t cr3;
    mx_handle_t apic_vmo;
#endif // __x86_64__
} mx_vcpu_create_args_t;

enum {
    MX_VCPU_STATE   = 1,
    MX_VCPU_IO      = 2,
};

// Structure to read and write VCPU state.
typedef struct mx_vcpu_state {
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
} mx_vcpu_state_t;

// Structure to read and write VCPU state for IO ports.
typedef struct mx_vcpu_io {
    uint8_t access_size;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint8_t data[4];
    };
} mx_vcpu_io_t;

__END_CDECLS
