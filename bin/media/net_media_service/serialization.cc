// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net_media_service/serialization.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace media {

Serializer::Serializer() {}

Serializer::~Serializer() {}

std::vector<uint8_t> Serializer::GetSerialMessage() {
  return std::move(serial_message_);
}

void Serializer::PutBytes(size_t count, const void* source) {
  if (count == 0) {
    return;
  }

  FTL_DCHECK(source != nullptr);

  size_t prev_size = serial_message_.size();
  serial_message_.resize(prev_size + count);
  std::memcpy(serial_message_.data() + prev_size, source, count);
}

Serializer& Serializer::operator<<(bool value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(uint8_t value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(uint16_t value) {
  value = htobe16(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(uint32_t value) {
  value = htobe32(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(uint64_t value) {
  value = htobe64(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(int8_t value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(int16_t value) {
  value = htobe16(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(int32_t value) {
  value = htobe32(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& Serializer::operator<<(int64_t value) {
  value = htobe64(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

Serializer& operator<<(Serializer& serializer, const std::string& value) {
  serializer << value.size();
  serializer.PutBytes(value.size(), value.data());
  return serializer;
}

Deserializer::Deserializer(std::vector<uint8_t> serial_message)
    : serial_message_(std::move(serial_message)) {}

Deserializer::~Deserializer() {}

bool Deserializer::GetBytes(size_t count, void* dest) {
  const uint8_t* bytes = Bytes(count);
  if (bytes == nullptr) {
    return false;
  }

  if (count == 0) {
    return true;
  }

  FTL_DCHECK(dest != nullptr);
  std::memcpy(dest, bytes, count);
  return true;
}

const uint8_t* Deserializer::Bytes(size_t count) {
  if (!healthy_ || serial_message_.size() - bytes_consumed_ < count) {
    healthy_ = false;
    return nullptr;
  }

  const uint8_t* result = serial_message_.data() + bytes_consumed_;
  bytes_consumed_ += count;
  return result;
}

Deserializer& Deserializer::operator>>(bool& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

Deserializer& Deserializer::operator>>(uint8_t& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

Deserializer& Deserializer::operator>>(uint16_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh16(value);
  return *this;
}

Deserializer& Deserializer::operator>>(uint32_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh32(value);
  return *this;
}

Deserializer& Deserializer::operator>>(uint64_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh64(value);
  return *this;
}

Deserializer& Deserializer::operator>>(int8_t& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

Deserializer& Deserializer::operator>>(int16_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh16(value);
  return *this;
}

Deserializer& Deserializer::operator>>(int32_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh32(value);
  return *this;
}

Deserializer& Deserializer::operator>>(int64_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh64(value);
  return *this;
}

Deserializer& operator>>(Deserializer& deserializer, std::string& value) {
  size_t size;
  deserializer >> size;
  const char* bytes = reinterpret_cast<const char*>(deserializer.Bytes(size));
  if (bytes == nullptr) {
    value = std::string();
  } else {
    value = std::string(bytes, size);
  }

  return deserializer;
}

}  // namespace media
