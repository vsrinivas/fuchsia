// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/protocol_helpers.h"

namespace debug_ipc {

using UpdateExceptionStrategy = UpdateGlobalSettingsRequest::UpdateExceptionStrategy;

void Serialize(const std::string& s, MessageWriter* writer) { writer->WriteString(s); }

bool Deserialize(MessageReader* reader, std::string* s) { return reader->ReadString(s); }

void Serialize(int64_t data, MessageWriter* writer) { writer->WriteInt64(data); }

bool Deserialize(MessageReader* reader, int64_t* data) { return reader->ReadInt64(data); }

void Serialize(uint64_t data, MessageWriter* writer) { writer->WriteUint64(data); }

bool Deserialize(MessageReader* reader, uint64_t* data) { return reader->ReadUint64(data); }

void Serialize(int32_t data, MessageWriter* writer) { writer->WriteInt32(data); }

bool Deserialize(MessageReader* reader, int32_t* data) { return reader->ReadInt32(data); }

void Serialize(const debug::Status& status, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(status.type()));
  writer->WriteUint64(status.platform_error());
  Serialize(status.message(), writer);
}

bool Deserialize(MessageReader* reader, debug::Status* status) {
  uint32_t type;
  if (!reader->ReadUint32(&type) || type >= debug::Status::kLast)
    return false;

  // The platform error value should be 0 for anything but kPlatformError types.
  uint64_t platform_error;
  if (!reader->ReadUint64(&platform_error) ||
      (type != debug::Status::kPlatformError && platform_error != 0))
    return false;

  std::string msg;
  if (!Deserialize(reader, &msg))
    return false;

  *status = debug::Status(debug::Status::InternalValues(), static_cast<debug::Status::Type>(type),
                          platform_error, std::move(msg));
  return true;
}

void Serialize(const ProcessThreadId& ids, MessageWriter* writer) {
  writer->WriteUint64(ids.process);
  writer->WriteUint64(ids.thread);
}

bool Deserialize(MessageReader* reader, ProcessThreadId* ids) {
  if (!reader->ReadUint64(&ids->process))
    return false;
  return reader->ReadUint64(&ids->thread);
}

void Serialize(const debug::RegisterValue& reg, MessageWriter* writer) {
  writer->WriteUint32(*reinterpret_cast<const uint32_t*>(&reg.id));
  writer->WriteUint32(static_cast<uint32_t>(reg.data.size()));
  writer->WriteBytes(reg.data.data(), static_cast<uint32_t>(reg.data.size()));
}

bool Deserialize(MessageReader* reader, debug::RegisterValue* reg) {
  if (!reader->ReadUint32(reinterpret_cast<uint32_t*>(&reg->id)))
    return false;
  uint32_t length;
  if (!reader->ReadUint32(&length))
    return false;
  reg->data.resize(length);
  return reader->ReadBytes(length, reg->data.data());
}

void Serialize(debug::RegisterCategory type, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(type));
}

bool Deserialize(MessageReader* reader, debug::RegisterCategory* type) {
  return reader->ReadUint32(reinterpret_cast<uint32_t*>(type));
}

void Serialize(const debug::AddressRange& range, MessageWriter* writer) {
  writer->WriteUint64(range.begin());
  writer->WriteUint64(range.end());
}

bool Deserialize(MessageReader* reader, debug::AddressRange* range) {
  uint64_t begin, end;
  if (!reader->ReadUint64(&begin) || !reader->ReadUint64(&end) || end < begin)
    return false;

  *range = debug::AddressRange(begin, end);
  return true;
}

void Serialize(ExceptionType type, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(type));
}

bool Deserialize(MessageReader* reader, ExceptionType* type) {
  uint32_t type32;
  if (!reader->ReadUint32(&type32) || type32 >= static_cast<uint32_t>(ExceptionType::kLast)) {
    return false;
  }
  *type = static_cast<ExceptionType>(type32);
  return true;
}

void Serialize(ExceptionStrategy strategy, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(strategy));
}

bool Deserialize(MessageReader* reader, ExceptionStrategy* strategy) {
  uint32_t strategy32;
  if (!reader->ReadUint32(&strategy32) ||
      strategy32 >= static_cast<uint32_t>(ExceptionStrategy::kLast)) {
    return false;
  }
  *strategy = static_cast<ExceptionStrategy>(strategy32);
  return true;
}

void Serialize(UpdateExceptionStrategy update, MessageWriter* writer) {
  Serialize(update.type, writer);
  Serialize(update.value, writer);
}

bool Deserialize(MessageReader* reader, UpdateExceptionStrategy* update) {
  if (!Deserialize(reader, &update->type)) {
    return false;
  }
  return Deserialize(reader, &update->value);
}

}  // namespace debug_ipc
