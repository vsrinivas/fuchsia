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
  bool first_tag;
  char* user_tag;
  bool has_msg;
  void WriteChar(const char value) {
    assert((offset + 1) < (reinterpret_cast<const char*>(this) + sizeof(LogBuffer)));
    *offset = value;
    offset++;
  }
  void WriteString(const char* c_str) {
    size_t len = strlen(c_str);
    assert((offset + len) < (reinterpret_cast<const char*>(this) + sizeof(LogBuffer)));
    memcpy(offset, c_str, len);
    offset += len;
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
