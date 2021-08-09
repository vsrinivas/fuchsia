// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_HELPERS_H_

#include <string>
#include <vector>

#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/address_range.h"
#include "src/developer/debug/shared/status.h"

namespace debug_ipc {

// This file contains common serializers and deserializers for types. If a
// type is only used once, the corresponding reader/writer would go in the
// [agent|client]_protocol.cc file that needs it.

// Trivial primitive type ones. These allow the vector serializer below to be
// used to [de]serialize a vector of strings or ints.
void Serialize(const std::string& s, MessageWriter* writer);
bool Deserialize(MessageReader* reader, std::string* s);
void Serialize(int64_t data, MessageWriter* writer);
bool Deserialize(MessageReader* reader, int64_t* data);
void Serialize(uint64_t data, MessageWriter* writer);
bool Deserialize(MessageReader* reader, uint64_t* data);
void Serialize(int32_t data, MessageWriter* writer);
bool Deserialize(MessageReader* reader, int32_t* data);

void Serialize(const debug::Status& status, MessageWriter* writer);
bool Deserialize(MessageReader* reader, debug::Status* status);

void Serialize(const ProcessThreadId& ids, MessageWriter* writer);
bool Deserialize(MessageReader* reader, ProcessThreadId* ids);

// Aggregate types that are serialized in both directions (otherwise the
// implementations would go into the client-/agent-specific file).
void Serialize(const debug::RegisterValue& reg, MessageWriter* writer);
bool Deserialize(MessageReader* reader, debug::RegisterValue* reg);

void Serialize(RegisterCategory type, MessageWriter* writer);
bool Deserialize(MessageReader* reader, RegisterCategory* reg_cat);

void Serialize(const debug::AddressRange& range, MessageWriter* writer);
bool Deserialize(MessageReader* reader, debug::AddressRange* range);

void Serialize(ExceptionType type, MessageWriter* writer);
bool Deserialize(MessageReader* reader, ExceptionType* type);

void Serialize(ExceptionStrategy strategy, MessageWriter* writer);
bool Deserialize(MessageReader* reader, ExceptionStrategy* strategy);

void Serialize(UpdateGlobalSettingsRequest::UpdateExceptionStrategy strategy,
               MessageWriter* writer);
bool Deserialize(MessageReader* reader,
                 UpdateGlobalSettingsRequest::UpdateExceptionStrategy* strategy);

// This can't be called "Serialize" because std::optional<T> is implicitly convertible from T and
// this implementation can start applying to regular (non-optional) values. Having a different name
// prevents serializing of arrays automatically.
//
// std::optional is serialized as a 32-bit "present" flag followed by the data if the flag is 1.
template <typename T>
inline void SerializeOptional(const std::optional<T>& v, MessageWriter* writer) {
  if (v) {
    writer->WriteUint32(1);
    Serialize(*v, writer);
  } else {
    writer->WriteUint32(0);
  }
}

template <typename T>
inline bool DeserializeOptional(MessageReader* reader, std::optional<T>* v) {
  uint32_t flag = 0;
  if (!reader->ReadUint32(&flag) || !(flag == 1 || flag == 0))
    return false;

  if (flag)
    return Deserialize(reader, &v->emplace());

  *v = std::nullopt;
  return true;
}

// Will call Serialize for each element in the vector.
template <typename T>
inline void Serialize(const std::vector<T>& v, MessageWriter* writer) {
  uint32_t size = static_cast<uint32_t>(v.size());
  writer->WriteUint32(size);
  for (uint32_t i = 0; i < size; i++)
    Serialize(v[i], writer);
}

template <>
inline void Serialize(const std::vector<uint8_t>& v, MessageWriter* writer) {
  uint32_t size = static_cast<uint32_t>(v.size());
  writer->WriteUint32(size);
  if (size > 0)
    writer->WriteBytes(v.data(), size);
}

template <>
inline void Serialize(const std::vector<InfoHandle>& v, MessageWriter* writer) {
  uint32_t size = static_cast<uint32_t>(v.size());
  writer->WriteUint32(size);
  if (size > 0)
    writer->WriteBytes(v.data(), size * sizeof(InfoHandle));
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

template <>
inline bool Deserialize(MessageReader* reader, std::vector<uint8_t>* v) {
  uint32_t size = 0;
  if (!reader->ReadUint32(&size))
    return false;
  v->resize(size);
  if (size == 0)
    return true;
  return reader->ReadBytes(size, v->data());
}

template <>
inline bool Deserialize(MessageReader* reader, std::vector<InfoHandle>* v) {
  uint32_t size = 0;
  if (!reader->ReadUint32(&size))
    return false;
  v->resize(size);
  if (size == 0)
    return true;
  return reader->ReadBytes(size * sizeof(InfoHandle), v->data());
}

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_PROTOCOL_HELPERS_H_
