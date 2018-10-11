// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This header file defines wire format to transfer logs to listening service.

#ifndef LIB_SYSLOG_WIRE_FORMAT_H_
#define LIB_SYSLOG_WIRE_FORMAT_H_

#include <lib/syslog/logger.h>
#include <zircon/types.h>

// Defines max length for storing log_metadata, tags and msgbuffer.
// TODO(anmittal): Increase it when zircon sockets are able to support a higher
// buffer.
#define FX_LOG_MAX_DATAGRAM_LEN (2032)

typedef struct fx_log_metadata {
    zx_koid_t pid;
    zx_koid_t tid;
    zx_time_t time;
    fx_log_severity_t severity;

    // Increment this field whenever there is a socket write error and client
    // drops the log and send it with next log msg.
    uint32_t dropped_logs;
} fx_log_metadata_t;

// Packet to transfer over socket.
typedef struct fx_log_packet {
    fx_log_metadata_t metadata;

    // Contains concatenated tags and message and a null terminating character at
    // the end.
    char data[FX_LOG_MAX_DATAGRAM_LEN - sizeof(fx_log_metadata_t)];
} fx_log_packet_t;

#endif // LIB_SYSLOG_WIRE_FORMAT_H_
