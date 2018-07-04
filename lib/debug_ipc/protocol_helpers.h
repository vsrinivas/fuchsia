// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/records.h"

namespace debug_ipc {

// This file contains common serializers and deserializers for types. If a
// type is only used once, the corresponding reader/writer would go in the
// [agent|client]_protocol.cc file that needs it.

// Trivial std::string ones. These allow the vector serializer below to be
// used to [de]serialize a vector of strings.
void Serialize(const std::string& s, MessageWriter* writer);
bool Deserialize(MessageReader* reader, std::string* s);

// Will call Serialize for each element in the vector.
template <typename T>
inline void Serialize(const std::vector<T>& v, MessageWriter* writer) {
  uint32_t size = static_cast<uint32_t>(v.size());
  writer->WriteUint32(size);
  for (uint32_t i = 0; i < size; i++)
    Serialize(v[i], writer);
}

// Will call Deserialize for each element in the vector.
template <typename T>
inline bool Deserialize(MessageReader* reader, std::vector<T>* v) {
  uint32_t size = 0;
  if (!reader->ReadUint32(&size))
    return false;
  v->resize(size);
  for (uint32_t i = 0; i < size; i++) {
    if (!Deserialize(reader, &(*v)[i]))
      return false;
  }
  return true;
}

}  // namespace debug_ipc
