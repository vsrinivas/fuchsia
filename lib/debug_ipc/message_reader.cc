// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/message_reader.h"

#include <string.h>

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

MessageReader::MessageReader(std::vector<char> message)
    : message_(std::move(message)) {}
MessageReader::~MessageReader() {}

bool MessageReader::ReadBytes(uint32_t len, void* output) {
  if (message_.size() - offset_ < len) return SetError();
  memcpy(output, &message_[offset_], len);
  offset_ += len;
  return true;
}

bool MessageReader::ReadInt32(int32_t* output) {
  return ReadBytes(sizeof(int32_t), output);
}
bool MessageReader::ReadUint32(uint32_t* output) {
  return ReadBytes(sizeof(uint32_t), output);
}
bool MessageReader::ReadInt64(int64_t* output) {
  return ReadBytes(sizeof(int64_t), output);
}
bool MessageReader::ReadUint64(uint64_t* output) {
  return ReadBytes(sizeof(uint64_t), output);
}

bool MessageReader::ReadString(std::string* output) {
  // Size header.
  uint32_t str_len;
  if (!ReadUint32(&str_len)) return SetError();
  if (str_len > remaining()) return SetError();

  // String bytes.
  output->resize(str_len);
  if (str_len == 0) return true;
  if (!ReadBytes(str_len, &(*output)[0])) {
    *output = std::string();
    return SetError();
  }
  return true;
}

bool MessageReader::ReadBool(bool* output) {
  uint32_t read;
  if (!ReadUint32(&read))
    return false;
  if (read != 0 && read != 1)
    return false;

  *output = !!read;
  return true;
}

bool MessageReader::ReadHeader(MsgHeader* output) {
  if (!ReadUint32(&output->size)) return false;

  uint32_t type;
  if (!ReadUint32(&type)) return false;
  if (type >= static_cast<uint32_t>(MsgHeader::Type::kNumMessages))
    return false;
  output->type = static_cast<MsgHeader::Type>(type);
  return ReadUint32(&output->transaction_id);
  ;
}

bool MessageReader::SetError() {
  has_error_ = true;
  return false;
}

}  // namespace debug_ipc
