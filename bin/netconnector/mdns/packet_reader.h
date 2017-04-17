// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

namespace netconnector {
namespace mdns {

// Reads values from a binary packet buffer.
class PacketReader {
 public:
  // Constructs a packet reader.
  PacketReader(std::vector<uint8_t> packet);

  ~PacketReader();

  // Determines whether this |PacketReader| has been successful so far.
  bool healthy() { return healthy_; }

  // Marks the deserializer unhealthy.
  void MarkUnhealthy() { healthy_ = false; }

  // Returns the number of bytes consumed so far.
  size_t bytes_consumed() { return bytes_consumed_; }

  // Returns the number of bytes remaining to be consumed.
  size_t bytes_remaining() { return packet_size_ - bytes_consumed_; }

  // Determines whether this |PacketReader| has successfully consumed the entire
  // packet.
  bool complete() { return healthy_ && bytes_consumed_ == packet_size_; }

  // Consumes |count| bytes from the packet and copies them to |dest| if at
  // least |count| bytes remain in the packet. If less than |count| bytes remain
  // in the the packet, this method returns false and |healthy| returns false
  // thereafter.
  bool GetBytes(size_t count, void* dest);

  // Consumes |count| bytes from the packet and returns a pointer to them if
  // at least |count| bytes remain in the packet. If less than |count| bytes
  // remain in the the packet, this method returns nullptr and |healthy| returns
  // false thereafter.
  const uint8_t* Bytes(size_t count);

  // Changes the read position (i.e. bytes consumed) to the specified value.
  // If the value is out of range, |healthy| returns false thereafter. Returns
  // the resulting value of |healthy()|.
  bool SetBytesConsumed(size_t bytes_consumed);

  // Changes the number of bytes remaining to the specified value. If the value
  // is out of range for the packet supplied in the constructor, |healthy|
  // returns false thereafter. Returns  the resulting value of |healthy()|.
  bool SetBytesRemaining(size_t bytes_remaining);

  PacketReader& operator>>(bool& value);
  PacketReader& operator>>(uint8_t& value);
  PacketReader& operator>>(uint16_t& value);
  PacketReader& operator>>(uint32_t& value);
  PacketReader& operator>>(uint64_t& value);
  PacketReader& operator>>(int8_t& value);
  PacketReader& operator>>(int16_t& value);
  PacketReader& operator>>(int32_t& value);
  PacketReader& operator>>(int64_t& value);

 private:
  bool healthy_ = true;
  std::vector<uint8_t> packet_;
  size_t packet_size_;
  size_t bytes_consumed_ = 0;
};

}  // namespace mdns
}  // namespace netconnector
