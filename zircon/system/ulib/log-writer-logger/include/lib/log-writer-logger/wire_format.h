// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header file defines wire format to transfer logs to listening service.

#ifndef LIB_LOG_WRITER_LOGGER_WIRE_FORMAT_H_
#define LIB_LOG_WRITER_LOGGER_WIRE_FORMAT_H_

#include <lib/log/log_writer.h>
#include <zircon/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Defines max length for storing log_metadata, tags and msgbuffer.
// TODO(anmittal): Increase it when zircon sockets are able to support a higher
// buffer.
#define LOG_MAX_DATAGRAM_LEN (2032)

typedef struct log_metadata {
  zx_koid_t pid;
  zx_koid_t tid;
  zx_time_t time;
  log_level_t level;

  // Increment this field whenever there is a socket write error and client
  // drops the log and send it with next log msg.
  uint32_t dropped_logs;
} log_metadata_t;

// Packet to transfer over socket.
typedef struct log_packet {
  log_metadata_t metadata;

  // Contains concatenated tags and message and a null terminating character at
  // the end.
  char data[LOG_MAX_DATAGRAM_LEN - sizeof(log_metadata_t)];
} log_packet_t;

#ifdef __cplusplus
}
#endif

#endif  // LIB_LOG_WRITER_LOGGER_WIRE_FORMAT_H_
