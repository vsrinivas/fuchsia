// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSCALLS_PORT_H_
#define ZIRCON_SYSCALLS_PORT_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off

// zx_object_wait_async() options
#define ZX_WAIT_ASYNC_ONCE          ((uint32_t)0u)
#define ZX_WAIT_ASYNC_REPEATING     ((uint32_t)1u)

// packet types.  zx_port_packet_t::type
#define ZX_PKT_TYPE_USER            ((uint8_t)0x00u)
#define ZX_PKT_TYPE_SIGNAL_ONE      ((uint8_t)0x01u)
#define ZX_PKT_TYPE_SIGNAL_REP      ((uint8_t)0x02u)
#define ZX_PKT_TYPE_GUEST_BELL      ((uint8_t)0x03u)
#define ZX_PKT_TYPE_GUEST_MEM       ((uint8_t)0x04u)
#define ZX_PKT_TYPE_GUEST_IO        ((uint8_t)0x05u)
#define ZX_PKT_TYPE_GUEST_VCPU      ((uint8_t)0x06u)
#define ZX_PKT_TYPE_INTERRUPT       ((uint8_t)0x07u)
#define ZX_PKT_TYPE_EXCEPTION(n)    ((uint32_t)(0x08u | (((n) & 0xFFu) << 8)))

#define ZX_PKT_TYPE_MASK            ((uint32_t)0x000000FFu)

#define ZX_PKT_IS_USER(type)        ((type) == ZX_PKT_TYPE_USER)
#define ZX_PKT_IS_SIGNAL_ONE(type)  ((type) == ZX_PKT_TYPE_SIGNAL_ONE)
#define ZX_PKT_IS_SIGNAL_REP(type)  ((type) == ZX_PKT_TYPE_SIGNAL_REP)
#define ZX_PKT_IS_GUEST_BELL(type)  ((type) == ZX_PKT_TYPE_GUEST_BELL)
#define ZX_PKT_IS_GUEST_MEM(type)   ((type) == ZX_PKT_TYPE_GUEST_MEM)
#define ZX_PKT_IS_GUEST_IO(type)    ((type) == ZX_PKT_TYPE_GUEST_IO)
#define ZX_PKT_IS_GUEST_VCPU(type)  ((type) == ZX_PKT_TYPE_GUEST_VCPU)
#define ZX_PKT_IS_INTERRUPT(type)   ((type) == ZX_PKT_TYPE_INTERRUPT)
#define ZX_PKT_IS_EXCEPTION(type)   (((type) & ZX_PKT_TYPE_MASK) == ZX_PKT_TYPE_EXCEPTION(0))

// zx_packet_guest_vcpu_t::type
#define ZX_PKT_GUEST_VCPU_INTERRUPT  ((uint8_t)0)
#define ZX_PKT_GUEST_VCPU_STARTUP    ((uint8_t)1)
// clang-format on

// port_packet_t::type ZX_PKT_TYPE_USER.
typedef union zx_packet_user {
    uint64_t u64[4];
    uint32_t u32[8];
    uint16_t u16[16];
    uint8_t c8[32];
} zx_packet_user_t;

// port_packet_t::type ZX_PKT_TYPE_SIGNAL_ONE and ZX_PKT_TYPE_SIGNAL_REP.
typedef struct zx_packet_signal {
    zx_signals_t trigger;
    zx_signals_t observed;
    uint64_t count;
    uint64_t reserved0;
    uint64_t reserved1;
} zx_packet_signal_t;

typedef struct zx_packet_exception {
    uint64_t pid;
    uint64_t tid;
    uint64_t reserved0;
    uint64_t reserved1;
} zx_packet_exception_t;

typedef struct zx_packet_guest_bell {
    zx_vaddr_t addr;
    uint64_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
} zx_packet_guest_bell_t;

typedef struct zx_packet_guest_mem {
    zx_vaddr_t addr;
#if __aarch64__
    uint8_t access_size;
    bool sign_extend;
    uint8_t xt;
    bool read;
    uint64_t data;
    uint64_t reserved;
#elif __x86_64__
// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
#define X86_MAX_INST_LEN 15u
    uint8_t inst_len;
    uint8_t inst_buf[X86_MAX_INST_LEN];
    uint8_t default_operand_size;
    uint8_t reserved[7];
#endif
} zx_packet_guest_mem_t;

typedef struct zx_packet_guest_io {
    uint16_t port;
    uint8_t access_size;
    bool input;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint8_t data[4];
    };
    uint64_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
} zx_packet_guest_io_t;

typedef struct zx_packet_guest_vcpu {
    uint8_t type;
    union {
        struct {
            uint32_t mask;
            uint8_t vector;
        } interrupt;
        struct {
            uint64_t id;
            zx_vaddr_t entry;
        } startup;
    };
    uint64_t reserved;
} zx_packet_guest_vcpu_t;

typedef struct zx_packet_interrupt {
    zx_time_t timestamp;
} zx_packet_interrupt_t;

typedef struct zx_port_packet {
    uint64_t key;
    uint32_t type;
    int32_t status;
    union {
        zx_packet_user_t user;
        zx_packet_signal_t signal;
        zx_packet_exception_t exception;
        zx_packet_guest_bell_t guest_bell;
        zx_packet_guest_mem_t guest_mem;
        zx_packet_guest_io_t guest_io;
        zx_packet_guest_vcpu_t guest_vcpu;
        zx_packet_interrupt_t interrupt;
    };
} zx_port_packet_t;

__END_CDECLS

#endif // ZIRCON_SYSCALLS_PORT_H_
