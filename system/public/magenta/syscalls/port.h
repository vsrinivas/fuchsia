// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// mx_port_create() options.
#define MX_PORT_OPT_V1 0u
#define MX_PORT_OPT_V2 1u

// mx_port V1 packet structures.

#define MX_PORT_MAX_PKT_SIZE       128u

#define MX_PORT_PKT_TYPE_KERN        0u
#define MX_PORT_PKT_TYPE_IOSN        1u
#define MX_PORT_PKT_TYPE_USER        2u
#define MX_PORT_PKT_TYPE_EXCEPTION   3u

typedef struct mx_packet_header {
    uint64_t key;
    uint32_t type;
    uint32_t extra;
} mx_packet_header_t;

typedef struct mx_io_packet {
    mx_packet_header_t hdr;
    mx_time_t timestamp;
    size_t bytes;
    mx_signals_t signals;
    uint32_t reserved;
} mx_io_packet_t;

// mx_port V2 packet structures.

#define MX_WAIT_ASYNC_ONCE          0u
#define MX_WAIT_ASYNC_REPEATING     1u

// packet types.
#define MX_PKT_TYPE_USER            0x00u
#define MX_PKT_TYPE_SIGNAL_ONE      0x01u
#define MX_PKT_TYPE_SIGNAL_REP      0x02u
#define MX_PKT_TYPE_EXCEPTION(n)    (0x03u | (((n) & 0xFFu) << 8))

#define MX_PKT_TYPE_MASK            0xFFu

#define MX_PKT_IS_USER(type)        ((type) == MX_PKT_TYPE_USER)
#define MX_PKT_IS_SIGNAL_ONE(type)  ((type) == MX_PKT_TYPE_SIGNAL_ONE)
#define MX_PKT_IS_SIGNAL_REP(type)  ((type) == MX_PKT_TYPE_SIGNAL_REP)
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
} mx_packet_signal_t;

typedef struct mx_port_packet {
    uint64_t key;
    uint32_t type;
    int32_t status;
    union {
        mx_packet_user_t user;
        mx_packet_signal_t signal;
    };
} mx_port_packet_t;


__END_CDECLS
