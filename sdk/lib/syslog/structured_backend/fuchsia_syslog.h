// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_
#define LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

// Max size of log buffer
#define FUCHSIA_SYSLOG_BUFFER_SIZE ((1 << 15) / 8)

// Additional storage for internal log state.
#define FUCHSIA_SYSLOG_STATE_SIZE (15)

// Opaque structure representing the backend encode state.
// This structure only has meaning to the backend and application code shouldn't
// touch these values.
// LogBuffers store the state of a log record that is in the process of being
// encoded.
// A LogBuffer is initialized by calling BeginRecord, is finalized (made read-only)
// by calling EndRecord, and is written to the LogSink by calling FlushRecord.
// Calling BeginRecord on a LogBuffer will always initialize it to its
// clean state.
typedef struct log_buffer {
  // Record state (for keeping track of backend-specific details)
  uint64_t record_state[FUCHSIA_SYSLOG_STATE_SIZE];

  // Log data (used by the backend to encode the log into). The format
  // for this is backend-specific.
  uint64_t data[FUCHSIA_SYSLOG_BUFFER_SIZE];
} log_buffer_t;

typedef int8_t LogSeverity;
// Default log levels.
#define FUCHSIA_LOG_TRACE (0x10)
#define FUCHSIA_LOG_DEBUG (0x20)
#define FUCHSIA_LOG_INFO (0x30)
#define FUCHSIA_LOG_WARNING (0x40)
#define FUCHSIA_LOG_ERROR (0x50)
#define FUCHSIA_LOG_FATAL (0x60)

#define FUCHSIA_LOG_NONE (0x00)

#define FUCHSIA_LOG_SEVERITY_STEP_SIZE (0x10)
#define FUCHSIA_LOG_VERBOSITY_STEP_SIZE (0x1)
__BEGIN_CDECLS
// Initializes a LogBuffer
// buffer -- The buffer to initialize

// severity -- The severity of the log
// file_name -- The name of the file that generated the log message

// line -- The line number that caused this message to be generated

// message -- The message to output. OWNERSHIP: If severity is LOG_FATAL
// then the caller maintains ownership of the message buffer and MUST NOT
// mutate of free the string until FlushRecord is called or the buffer is reset/discarded
// with another call to BeginRecord.

// condition -- Does nothing. Exists solely for compatibility with legacy code.

// is_printf -- Whether or not this is a printf message. If true,
// the message should be interpreted as a C-style printf before being displayed to the
// user.

// socket -- The socket to write the message to.

// dropped_count -- Number of dropped messages

// pid -- The process ID that generated the message.

// tid -- The thread ID that generated the message.
void syslog_begin_record(log_buffer_t* buffer, LogSeverity severity, const char* file_name,
                         size_t file_name_length, unsigned int line, const char* message,
                         size_t message_length, const char* condition, size_t condition_length,
                         bool is_printf, zx_handle_t socket, uint32_t dropped_count, zx_koid_t pid,
                         zx_koid_t tid);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_string(log_buffer_t* buffer, const char* key, size_t key_length,
                                   const char* value, size_t value_length);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_int64(log_buffer_t* buffer, const char* key, size_t key_length,
                                  int64_t value);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_uint64(log_buffer_t* buffer, const char* key, size_t key_length,
                                   uint64_t value);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_double(log_buffer_t* buffer, const char* key, size_t key_length,
                                   double value);

// Writes the LogBuffer to the socket.
bool syslog_flush_record(log_buffer_t* buffer);
__END_CDECLS
#endif  // LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_
