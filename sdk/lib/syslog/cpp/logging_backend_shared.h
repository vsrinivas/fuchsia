// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include "log_level.h"
#include "macros.h"

// This file contains shared implementations for writing string logs between the legacy backend and
// the host backend

namespace syslog_backend {
struct MsgHeader {
  syslog::LogSeverity severity;
  char* offset;
  LogBuffer* buffer;
  bool first_tag;
  char* user_tag;
  bool has_msg;
  size_t RemainingSpace() {
    auto end = (reinterpret_cast<const char*>(this) + sizeof(LogBuffer));
    auto avail = static_cast<size_t>(end - offset - 1);
    return avail;
  }

  void WriteChar(const char value) {
    if (!RemainingSpace()) {
      FlushRecord(buffer);
      offset = reinterpret_cast<char*>(buffer->data);
      WriteString("CONTINUATION: ");
    }
    assert((offset + 1) < (reinterpret_cast<const char*>(this) + sizeof(LogBuffer)));
    *offset = value;
    offset++;
  }

  void WriteString(const char* c_str) {
    size_t total_chars = strlen(c_str);
    size_t written_chars = 0;
    while (written_chars < total_chars) {
      size_t written = WriteStringInternal(c_str + written_chars);
      written_chars += written;
      if (written_chars < total_chars) {
        FlushAndReset();
        WriteStringInternal("CONTINUATION: ");
      }
    }
  }

  void FlushAndReset() {
    FlushRecord(buffer);
    offset = reinterpret_cast<char*>(buffer->data);
  }

  // Writes a string to the buffer and returns the
  // number of bytes written. Returns 0 only if
  // the length of the string is 0, or if we're
  // exactly at the end of the buffer and need a reset.
  size_t WriteStringInternal(const char* c_str) {
    size_t len = strlen(c_str);
    auto remaining = RemainingSpace();
    if (len > remaining) {
      len = remaining;
    }
    assert((offset + len) < (reinterpret_cast<const char*>(this) + sizeof(LogBuffer)));
    memcpy(offset, c_str, len);
    offset += len;
    return len;
  }
  void Init(LogBuffer* buffer, syslog::LogSeverity severity) {
    this->severity = severity;
    user_tag = nullptr;
    offset = reinterpret_cast<char*>(buffer->data);
    first_tag = true;
    has_msg = false;
  }
  static MsgHeader* CreatePtr(LogBuffer* buffer) {
    return reinterpret_cast<MsgHeader*>(&buffer->record_state);
  }
};

#ifndef __Fuchsia__
const std::string GetNameForLogSeverity(syslog::LogSeverity severity);
#endif

static_assert(sizeof(MsgHeader) <= sizeof(LogBuffer::record_state));

void WritePreamble(LogBuffer* buffer);

}  // namespace syslog_backend
