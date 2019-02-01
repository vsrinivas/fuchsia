// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/message_writer.h"

#include <string.h>

namespace debug_ipc {

namespace {

constexpr uint32_t kInitialSize = 32;

}  // namespace

MessageWriter::MessageWriter() : MessageWriter(kInitialSize) {}

MessageWriter::MessageWriter(size_t initial_size) {
  buffer_.reserve(initial_size);
}

MessageWriter::~MessageWriter() {}

void MessageWriter::WriteBytes(const void* data, uint32_t len) {
  const char* begin = static_cast<const char*>(data);
  const char* end = begin + len;
  buffer_.insert(buffer_.end(), begin, end);
}

void MessageWriter::WriteInt32(int32_t i) { WriteBytes(&i, sizeof(int32_t)); }
void MessageWriter::WriteUint32(uint32_t i) {
  WriteBytes(&i, sizeof(uint32_t));
}
void MessageWriter::WriteInt64(int64_t i) { WriteBytes(&i, sizeof(int64_t)); }
void MessageWriter::WriteUint64(uint64_t i) {
  WriteBytes(&i, sizeof(uint64_t));
}

void MessageWriter::WriteString(const std::string& str) {
  // 32-bit size first, followed by bytes.
  uint32_t size = static_cast<uint32_t>(str.size());
  WriteUint32(size);
  if (!str.empty())
    WriteBytes(&str[0], size);
}

void MessageWriter::WriteBool(bool b) { WriteUint32(b ? 1u : 0u); }

void MessageWriter::WriteHeader(MsgHeader::Type type, uint32_t transaction_id) {
  WriteUint32(0);  // Size to be filled in by GetDataAndWriteSize() later.
  WriteUint32(static_cast<uint32_t>(type));
  WriteUint32(transaction_id);
}

std::vector<char> MessageWriter::MessageComplete() {
  uint32_t size = static_cast<uint32_t>(buffer_.size());
  memcpy(&buffer_[0], &size, sizeof(uint32_t));
  return std::move(buffer_);
}

}  // namespace debug_ipc
