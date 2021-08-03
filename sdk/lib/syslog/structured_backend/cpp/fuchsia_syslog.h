// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_STRUCTURED_BACKEND_CPP_FUCHSIA_SYSLOG_H_
#define LIB_SYSLOG_STRUCTURED_BACKEND_CPP_FUCHSIA_SYSLOG_H_

#include <lib/stdcompat/optional.h>
#include <lib/stdcompat/string_view.h>
#include <lib/syslog/structured_backend/fuchsia_syslog.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>
#include <stdint.h>

namespace fuchsia_syslog {

// Opaque structure representing the backend encode state.
// This structure only has meaning to the backend and application code shouldn't
// touch these values.
// LogBuffers store the state of a log record that is in the process of being
// encoded.
// A LogBuffer is initialized by calling BeginRecord,
// and is written to the LogSink by calling FlushRecord.
// Calling BeginRecord on a LogBuffer will always initialize it to its
// clean state.
class LogBuffer final {
 public:
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
  void BeginRecord(FuchsiaLogSeverity severity, cpp17::optional<cpp17::string_view> file_name,
                   unsigned int line, cpp17::optional<cpp17::string_view> message,
                   cpp17::optional<cpp17::string_view> condition, bool is_printf,
                   zx::unowned_socket socket, uint32_t dropped_count, zx_koid_t pid,
                   zx_koid_t tid) {
    syslog_begin_record(&data_, severity, StringViewToCStr(file_name), StringViewLength(file_name),
                        line, StringViewToCStr(message), StringViewLength(message),
                        StringViewToCStr(condition), StringViewLength(condition), is_printf,
                        socket->get(), dropped_count, pid, tid);
  }

  // Writes a key/value pair to the buffer.
  void WriteKeyValue(cpp17::string_view key, cpp17::string_view value) {
    syslog_write_key_value_string(&data_, StringViewToCStr(key), StringViewLength(key),
                                  StringViewToCStr(value), StringViewLength(value));
  }

  // Writes a key/value pair to the buffer.
  void WriteKeyValue(cpp17::string_view key, int64_t value) {
    syslog_write_key_value_int64(&data_, StringViewToCStr(key), StringViewLength(key), value);
  }

  // Writes a key/value pair to the buffer.
  void WriteKeyValue(cpp17::string_view key, uint64_t value) {
    syslog_write_key_value_uint64(&data_, StringViewToCStr(key), StringViewLength(key), value);
  }

  // Writes a key/value pair to the buffer.
  void WriteKeyValue(cpp17::string_view key, double value) {
    syslog_write_key_value_double(&data_, StringViewToCStr(key), StringViewLength(key), value);
  }

  // Writes the LogBuffer to the socket.
  bool FlushRecord() { return syslog_flush_record(&data_); }

 private:
  static const char* StringViewToCStr(cpp17::optional<cpp17::string_view> view) {
    if (!view.has_value()) {
      return nullptr;
    }
    return view.value().data();
  }
  static size_t StringViewLength(cpp17::optional<cpp17::string_view> view) {
    if (!view.has_value()) {
      return 0;
    }
    return view.value().length();
  }
  fuchsia_syslog_log_buffer_t data_;
};

}  // namespace fuchsia_syslog

#endif  // LIB_SYSLOG_STRUCTURED_BACKEND_CPP_FUCHSIA_SYSLOG_H_
