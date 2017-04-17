// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/mdns/packet_writer.h"

#include <endian.h>

#include "lib/ftl/logging.h"

namespace netconnector {
namespace mdns {

PacketWriter::PacketWriter() {}

PacketWriter::PacketWriter(std::vector<uint8_t> packet)
    : packet_(std::move(packet)) {}

PacketWriter::~PacketWriter() {}

std::vector<uint8_t> PacketWriter::GetResizedPacket() {
  position_ = 0;
  positions_by_label_.clear();
  packet_.resize(position_);
  return std::move(packet_);
}

std::vector<uint8_t> PacketWriter::GetPacket() {
  position_ = 0;
  positions_by_label_.clear();
  return std::move(packet_);
}

void PacketWriter::PutBytes(size_t count, const void* source) {
  if (count == 0) {
    return;
  }

  FTL_DCHECK(source != nullptr);

  if (packet_.size() < position_ + count) {
    packet_.resize(position_ + count);
  }

  std::memcpy(packet_.data() + position_, source, count);

  position_ += count;
}

void PacketWriter::CreateBookmark(const std::string& label) {
  positions_by_label_.emplace(label, position_);
}

size_t PacketWriter::GetBookmarkPosition(const std::string& label) const {
  auto iter = positions_by_label_.find(label);
  if (iter == positions_by_label_.end()) {
    return npos;
  }

  return iter->second;
}

PacketWriter& PacketWriter::operator<<(bool value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(uint8_t value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(uint16_t value) {
  value = htobe16(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(uint32_t value) {
  value = htobe32(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(uint64_t value) {
  value = htobe64(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(int8_t value) {
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(int16_t value) {
  value = htobe16(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(int32_t value) {
  value = htobe32(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(int64_t value) {
  value = htobe64(value);
  PutBytes(sizeof(value), &value);
  return *this;
}

PacketWriter& PacketWriter::operator<<(const std::vector<uint8_t>& value) {
  PutBytes(value.size(), value.data());
  return *this;
}

}  // namespace mdns
}  // namespace netconnector
