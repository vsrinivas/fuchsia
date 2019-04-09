// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/protocol_helpers.h"

namespace debug_ipc {

void Serialize(const std::string& s, MessageWriter* writer) {
  writer->WriteString(s);
}

bool Deserialize(MessageReader* reader, std::string* s) {
  return reader->ReadString(s);
}

void Serialize(uint64_t data, MessageWriter* writer) {
  writer->WriteUint64(data);
}

bool Deserialize(MessageReader* reader, uint64_t* data) {
  return reader->ReadUint64(data);
}

void Serialize(int32_t data, MessageWriter* writer) {
  writer->WriteInt32(data);
}

bool Deserialize(MessageReader* reader, int32_t* data) {
  return reader->ReadInt32(data);
}

void Serialize(const Register& reg, MessageWriter* writer) {
  writer->WriteUint32(*reinterpret_cast<const uint32_t*>(&reg.id));
  writer->WriteUint32(reg.data.size());
  writer->WriteBytes(&reg.data[0], reg.data.size());
}

bool Deserialize(MessageReader* reader, Register* reg) {
  if (!reader->ReadUint32(reinterpret_cast<uint32_t*>(&reg->id)))
    return false;
  uint32_t length;
  if (!reader->ReadUint32(&length))
    return false;
  reg->data.resize(length);
  return reader->ReadBytes(length, &reg->data[0]);
}

}  // namespace debug_ipc
