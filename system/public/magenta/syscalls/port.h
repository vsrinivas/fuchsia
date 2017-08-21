// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// mx_object_wait_async() options
#define MX_WAIT_ASYNC_ONCE          0u
#define MX_WAIT_ASYNC_REPEATING     1u

// packet types.
#define MX_PKT_TYPE_USER            0x00u
#define MX_PKT_TYPE_SIGNAL_ONE      0x01u
#define MX_PKT_TYPE_SIGNAL_REP      0x02u
#define MX_PKT_TYPE_GUEST_MEM       0x03u
#define MX_PKT_TYPE_GUEST_IO        0x04u
#define MX_PKT_TYPE_EXCEPTION(n)    (0x05u | (((n) & 0xFFu) << 8))

#define MX_PKT_TYPE_MASK            0xFFu

#define MX_PKT_IS_USER(type)        ((type) == MX_PKT_TYPE_USER)
#define MX_PKT_IS_SIGNAL_ONE(type)  ((type) == MX_PKT_TYPE_SIGNAL_ONE)
#define MX_PKT_IS_SIGNAL_REP(type)  ((type) == MX_PKT_TYPE_SIGNAL_REP)
#define MX_PKT_IS_GUEST_MEM(type)   ((type) == MX_PKT_TYPE_GUEST_MEM)
#define MX_PKT_IS_GUEST_IO(type)    ((type) == MX_PKT_TYPE_GUEST_IO)
#define MX_PKT_IS_EXCEPTION(type)   (((type) & MX_PKT_TYPE_MASK) == MX_PKT_TYPE_EXCEPTION(0))

// port_packet_t::type MX_PKT_TYPE_USER.
typedef union mx_packet_user {
    uint64_t u64[4];
    uint32_t u32[8];
    uint16_t u16[16];
    uint8_t   c8[32];
} mx_packet_user_t;

// port_packet_t::type MX_PKT_TYPE_SIGNAL_ONE and MX_PKT_TYPE_SIGNAL_REP.
typedef struct mx_packet_signal {
    mx_signals_t trigger;
    mx_signals_t observed;
    uint64_t count;
    uint64_t reserved0;
    uint64_t reserved1;
} mx_packet_signal_t;

typedef struct mx_packet_exception {
    uint64_t pid;
    uint64_t tid;
    uint64_t reserved0;
    uint64_t reserved1;
} mx_packet_exception_t;

typedef struct mx_packet_guest_mem {
    mx_vaddr_t addr;
#if __aarch64__
    uint32_t inst;
    uint32_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
#elif __x86_64__
// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
#define X86_MAX_INST_LEN 15u
    uint8_t inst_len;
    uint8_t inst_buf[X86_MAX_INST_LEN];
    uint64_t reserved;
#endif
} mx_packet_guest_mem_t;

typedef struct mx_packet_guest_io {
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
} mx_packet_guest_io_t;

typedef struct mx_port_packet {
    uint64_t key;
    uint32_t type;
    int32_t status;
    union {
        mx_packet_user_t user;
        mx_packet_signal_t signal;
        mx_packet_exception_t exception;
        mx_packet_guest_io_t guest_io;
        mx_packet_guest_mem_t guest_mem;
    };
} mx_port_packet_t;

__END_CDECLS
