// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls/types.h>
#include <magenta/syscalls/exception.h>

__BEGIN_CDECLS

// Defines and structures for mx_port_*()

#define MX_PORT_MAX_PKT_SIZE   128u

#define MX_PORT_PKT_TYPE_KERN      0u
#define MX_PORT_PKT_TYPE_IOSN      1u
#define MX_PORT_PKT_TYPE_USER      2u
#define MX_PORT_PKT_TYPE_EXCEPTION 3u

typedef struct mx_packet_header {
    uint64_t key;
    uint32_t type;
    uint32_t extra;
} mx_packet_header_t;

typedef struct mx_io_packet {
    mx_packet_header_t hdr;
    mx_time_t timestamp;
    mx_size_t bytes;
    mx_signals_t signals;
    uint32_t reserved;
} mx_io_packet_t;

typedef struct mx_exception_packet {
    mx_packet_header_t hdr;
    mx_exception_report_t report;
} mx_exception_packet_t;

__END_CDECLS