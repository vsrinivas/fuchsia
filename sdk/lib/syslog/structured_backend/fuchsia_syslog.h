// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_
#define LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
__BEGIN_CDECLS

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

// Max size of log buffer
#define FUCHSIA_SYSLOG_BUFFER_SIZE ((1 << 15) / 8)

// Additional storage for internal log state.
#define FUCHSIA_SYSLOG_STATE_SIZE (15)

// Printf key to use in printf arguments.
// This only has special meaning when the following conditions
// are met:
// * No other KVP has been encoded other than a printf KVP
// * BeginRecord was called with is_printf set to true
// * A valid format string was passed to message
// If an invalid format string is passed as the message
// parameter, the log message will be considered invalid.
// This may result in an error message being output
// to the log destination, the failure to print the message
// entirely, or the log message being interpreted as a
// regular KVP message and rendered as such by the log
// consumer.
#define FUCHSIA_SYSLOG_PRINTF_KEY ""

// Opaque structure representing the backend encode state.
// This structure only has meaning to the backend and application code shouldn't
// touch these values.
// LogBuffers store the state of a log record that is in the process of being
// encoded.
// A LogBuffer is initialized by calling BeginRecord, and is written to
// the LogSink by calling FlushRecord.
// Calling BeginRecord on a LogBuffer will always initialize it to its
// clean state.
typedef struct fuchsia_log_buffer {
  // Record state (for keeping track of backend-specific details)
  uint64_t record_state[FUCHSIA_SYSLOG_STATE_SIZE];

  // Log data (used by the backend to encode the log into). The format
  // for this is backend-specific.
  uint64_t data[FUCHSIA_SYSLOG_BUFFER_SIZE];
} fuchsia_syslog_log_buffer_t;

typedef int8_t FuchsiaLogSeverity;

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
void syslog_begin_record(fuchsia_syslog_log_buffer_t* buffer, FuchsiaLogSeverity severity,
                         const char* file_name, size_t file_name_length, unsigned int line,
                         const char* message, size_t message_length, const char* condition,
                         size_t condition_length, bool is_printf, zx_handle_t socket,
                         uint32_t dropped_count, zx_koid_t pid, zx_koid_t tid);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_string(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, const char* value, size_t value_length);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_int64(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                  size_t key_length, int64_t value);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_uint64(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, uint64_t value);

// Writes a key/value pair to the buffer.
void syslog_write_key_value_double(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, double value);

// Writes the LogBuffer to the socket.
bool syslog_flush_record(fuchsia_syslog_log_buffer_t* buffer);

__END_CDECLS

#endif  // LIB_SYSLOG_STRUCTURED_BACKEND_FUCHSIA_SYSLOG_H_
