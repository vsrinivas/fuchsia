// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>
#include <iosfwd>
#include "varint.h"

namespace overnet {

// Identifier of an active stream of communication between two nodes.
class StreamId {
 public:
  explicit StreamId(uint64_t id) : id_(id) {}
  bool operator==(StreamId other) const { return id_ == other.id_; }
  bool operator!=(StreamId other) const { return id_ != other.id_; }

  uint64_t Hash() const { return id_; }
  uint64_t get() const { return id_; }
  std::string ToString() const;

  uint8_t wire_length() const { return varint::WireSizeFor(id_); }
  uint8_t* Write(uint8_t wire_length, uint8_t* dst) const {
    return varint::Write(id_, wire_length, dst);
  }

 private:
  uint64_t id_;
};

std::ostream& operator<<(std::ostream& out, StreamId stream_id);

}  // namespace overnet

namespace std {
template <>
struct hash<overnet::StreamId> {
  size_t operator()(overnet::StreamId id) const { return id.Hash(); }
};
}  // namespace std
