// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging_backend_shared.h"

#include <inttypes.h>

namespace syslog_backend {

void WritePreamble(LogBuffer* buffer) {
  auto header = MsgHeader::CreatePtr(buffer);
  if (header->first_tag) {
    header->first_tag = false;
    // Sometimes we have no message. Why add an extra space if that's the case?
    if (header->has_msg) {
      header->WriteChar(' ');
    }
    header->WriteChar('{');
  } else {
    header->WriteString(", ");
  }
}

void BeginRecord(LogBuffer* buffer, syslog::LogSeverity severity, const char* file,
                 unsigned int line, const char* msg, const char* condition) {
  auto header = MsgHeader::CreatePtr(buffer);
  header->Init(buffer, severity);
#ifndef __Fuchsia__
  auto severity_string = GetNameForLogSeverity(severity);
  header->WriteString(severity_string.data());
  header->WriteString(": ");
#endif
  header->WriteChar('[');
  header->WriteString(file);
  header->WriteChar('(');
  char a_buffer[128];
  snprintf(a_buffer, 128, "%i", line);
  header->WriteString(a_buffer);
  header->WriteString(")] ");
  if (condition) {
    header->WriteString("Check failed: ");
    header->WriteString(condition);
    header->WriteString(". ");
  }
  if (msg) {
    header->WriteString(msg);
    header->has_msg = true;
  }
}

void WriteKeyValue(LogBuffer* buffer, const char* key, const char* value) {
  auto header = MsgHeader::CreatePtr(buffer);
  // "tag" has special meaning to our logging API
  if (!strcmp("tag", key)) {
    auto tag_size = strlen(value) + 1;
    header->user_tag = (reinterpret_cast<char*>(buffer->data) + sizeof(buffer->data)) - tag_size;
    memcpy(header->user_tag, value, tag_size);
    return;
  }
  WritePreamble(buffer);
  // Key
  header->WriteChar('"');
  header->WriteString(key);
  header->WriteChar('"');
  // Value
  header->WriteString(": \"");
  header->WriteString(value);
  header->WriteChar('"');
}

void WriteKeyValue(LogBuffer* buffer, const char* key, int64_t value) {
  auto header = MsgHeader::CreatePtr(buffer);
  WritePreamble(buffer);
  // Key
  header->WriteChar('"');
  header->WriteString(key);
  header->WriteChar('"');
  // Value
  header->WriteString(": ");
  char a_buffer[128];
  snprintf(a_buffer, 128, "%" PRId64, value);
  header->WriteString(a_buffer);
}

void WriteKeyValue(LogBuffer* buffer, const char* key, uint64_t value) {
  auto header = MsgHeader::CreatePtr(buffer);
  WritePreamble(buffer);
  // Key
  header->WriteChar('"');
  header->WriteString(key);
  header->WriteChar('"');
  // Value
  header->WriteString(": ");
  char a_buffer[128];
  snprintf(a_buffer, 128, "%" PRIu64, value);
  header->WriteString(a_buffer);
}

void WriteKeyValue(LogBuffer* buffer, const char* key, double value) {
  auto header = MsgHeader::CreatePtr(buffer);
  WritePreamble(buffer);
  // Key
  header->WriteChar('"');
  header->WriteString(key);
  header->WriteChar('"');
  // Value
  header->WriteString(": ");
  char a_buffer[128];
  snprintf(a_buffer, 128, "%f", value);
  header->WriteString(a_buffer);
}

void EndRecord(LogBuffer* buffer) {
  auto header = MsgHeader::CreatePtr(buffer);
  if (!header->first_tag) {
    header->WriteChar('}');
  }
}
}  // namespace syslog_backend
