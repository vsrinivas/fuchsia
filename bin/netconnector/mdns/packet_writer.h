// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace netconnector {
namespace mdns {

// Writes values into a binary packet.
class PacketWriter {
 public:
  static constexpr size_t npos = std::numeric_limits<size_t>::max();

  template <typename T>
  static std::vector<uint8_t> Write(const T& t) {
    PacketWriter writer;
    writer << t;
    return writer.GetPacket();
  }

  // Creates a packet writer with an empty packet vector.
  PacketWriter();

  // Creates a packet writer that writes to the supplied vector. The intended
  // pattern here is that a buffer is moved into the writer using a call like
  // |PacketWriter(std::move(my_buffer))| and moved back out when writing is
  // complete using |GetPacket|. Call |position| before calling |GetPacket| to
  // retrieve the size of the written data. In this way, a fixed-length buffer
  // may be used repeatedly without the allocations implied by creating a new
  // buffer or resizing an existing one. Note that the buffer *will* be resized
  // if it's too small to accommodate the written packet.
  explicit PacketWriter(std::vector<uint8_t> packet);

  ~PacketWriter();

  // Gets the current position.
  size_t position() const { return position_; }

  // Sets the current position.
  void SetPosition(size_t position) { position_ = position; }

  // Gets the packet vector and resets this |PacketWriter| after resizing the
  // vector to |position()| bytes. If the caller is using the default
  // constructor, allowing the writer to create its own buffer, this is a
  // good way to get that buffer out of the writer once writing is complete.
  // To reuse a fixed-size buffer, use the constructor that takes a packet
  // parameter, |position| to get the written size and |GetPacket| to take back
  // the buffer.
  std::vector<uint8_t> GetResizedPacket();

  // Gets the unresized packet vector and resets this |PacketWriter|. To
  // size the size of the written packet, call |position()| before calling this
  // method.
  std::vector<uint8_t> GetPacket();

  // Puts |count| bytes from |source| into the packet.
  void PutBytes(size_t count, const void* source);

  // Creates a bookmark for the current position.
  void CreateBookmark(const std::string& label);

  // Gets the position for a bookmark (established by calling |CreateBookmark|).
  // Returns |npos| if the bookmark isn't found.
  size_t GetBookmarkPosition(const std::string& label) const;

  PacketWriter& operator<<(bool value);
  PacketWriter& operator<<(uint8_t value);
  PacketWriter& operator<<(uint16_t value);
  PacketWriter& operator<<(uint32_t value);
  PacketWriter& operator<<(uint64_t value);
  PacketWriter& operator<<(int8_t value);
  PacketWriter& operator<<(int16_t value);
  PacketWriter& operator<<(int32_t value);
  PacketWriter& operator<<(int64_t value);
  PacketWriter& operator<<(const std::vector<uint8_t>& value);

 private:
  std::vector<uint8_t> packet_;
  size_t position_ = 0;
  std::unordered_map<std::string, size_t> positions_by_label_;
};

}  // namespace mdns
}  // namespace netconnector
