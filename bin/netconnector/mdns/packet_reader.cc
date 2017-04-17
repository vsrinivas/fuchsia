// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/packet_reader.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

PacketReader::PacketReader(std::vector<uint8_t> packet)
    : packet_(std::move(packet)), packet_size_(packet_.size()) {}

PacketReader::~PacketReader() {}

bool PacketReader::GetBytes(size_t count, void* dest) {
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

const uint8_t* PacketReader::Bytes(size_t count) {
  if (!healthy_ || packet_size_ - bytes_consumed_ < count) {
    healthy_ = false;
    return nullptr;
  }

  const uint8_t* result = packet_.data() + bytes_consumed_;
  bytes_consumed_ += count;
  return result;
}

bool PacketReader::SetBytesConsumed(size_t bytes_consumed) {
  if (bytes_consumed > packet_size_) {
    healthy_ = false;
    return false;
  }

  bytes_consumed_ = bytes_consumed;
  return healthy_;
}

bool PacketReader::SetBytesRemaining(size_t bytes_remaining) {
  if (bytes_remaining + bytes_consumed_ > packet_.size()) {
    healthy_ = false;
    return false;
  }

  packet_size_ = bytes_remaining + bytes_consumed_;
  return healthy_;
}

PacketReader& PacketReader::operator>>(bool& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

PacketReader& PacketReader::operator>>(uint8_t& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

PacketReader& PacketReader::operator>>(uint16_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh16(value);
  return *this;
}

PacketReader& PacketReader::operator>>(uint32_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh32(value);
  return *this;
}

PacketReader& PacketReader::operator>>(uint64_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh64(value);
  return *this;
}

PacketReader& PacketReader::operator>>(int8_t& value) {
  GetBytes(sizeof(value), &value);
  return *this;
}

PacketReader& PacketReader::operator>>(int16_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh16(value);
  return *this;
}

PacketReader& PacketReader::operator>>(int32_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh32(value);
  return *this;
}

PacketReader& PacketReader::operator>>(int64_t& value) {
  GetBytes(sizeof(value), &value);
  value = betoh64(value);
  return *this;
}

}  // namespace mdns
}  // namespace netconnector
