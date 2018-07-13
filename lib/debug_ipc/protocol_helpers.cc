// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/protocol_helpers.h"

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

}  // namespace debug_ipc
