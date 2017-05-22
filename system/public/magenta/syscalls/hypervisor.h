// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <magenta/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// Opcodes for mx_hypervisor_op().

#define MX_HYPERVISOR_OP_GUEST_CREATE           1u
#define MX_HYPERVISOR_OP_GUEST_ENTER            2u
#define MX_HYPERVISOR_OP_GUEST_MEM_TRAP         3u

#define MX_HYPERVISOR_OP_GUEST_SET_GPR          4u
#define MX_HYPERVISOR_OP_GUEST_GET_GPR          5u

#define MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP     6u

#if __x86_64__
#define MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3    7u
#define MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM     8u
#endif // __x86_64__

typedef struct mx_guest_gpr {
#if __aarch64__
    uint64_t r[31];
#elif __x86_64__
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
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
#else
#error Unsupported architecture
#endif
} mx_guest_gpr_t;

// Packets for communication over the control FIFO.

#define MX_GUEST_PKT_TYPE_IO_PORT               1u
#define MX_GUEST_PKT_TYPE_MEM_TRAP              2u
#define MX_GUEST_PKT_TYPE_MEM_TRAP_ACTION       3u

typedef struct mx_guest_io_port {
    uint8_t access_size;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint8_t data[4];
    };
} mx_guest_io_port_t;

typedef struct mx_guest_mem_trap {
#if __aarch64__
    uint32_t instruction;
#elif __x86_64__
    uint8_t instruction_length;
    // NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
    uint8_t instruction_buffer[15];
#else
#error Unsupported architecture
#endif
    mx_vaddr_t guest_paddr;
} mx_guest_mem_trap_t;

typedef struct mx_guest_mem_trap_action {
    bool fault;
} mx_guest_mem_trap_action_t;

typedef struct mx_guest_packet {
    uint8_t type;
    union {
        mx_guest_io_port_t io_port;
        mx_guest_mem_trap_t mem_trap;
        mx_guest_mem_trap_action_t mem_trap_action;
    };
} mx_guest_packet_t;

#define MX_GUEST_MAX_PKT_SIZE                   32u
static_assert(sizeof(mx_guest_packet_t) <= MX_GUEST_MAX_PKT_SIZE,
              "size of mx_guest_packet_t must not exceed "
              "MX_GUEST_MAX_PKT_SIZE");

__END_CDECLS
