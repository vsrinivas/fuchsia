// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/overnet/protocol/cpp/fidl.h>
#include <stdint.h>

#include <functional>
#include <iosfwd>

#include "src/connectivity/overnet/lib/protocol/varint.h"

namespace overnet {

// Identifier of an active stream of communication between two nodes.
class StreamId {
 public:
  explicit StreamId(uint64_t id) : id_(id) {}
  StreamId(fuchsia::overnet::protocol::StreamId id) : id_(id.id) {}

  uint64_t Hash() const { return id_; }
  uint64_t get() const { return id_; }
  std::string ToString() const;

  uint8_t wire_length() const { return varint::WireSizeFor(id_); }
  uint8_t* Write(uint8_t wire_length, uint8_t* dst) const {
    return varint::Write(id_, wire_length, dst);
  }

  fuchsia::overnet::protocol::StreamId as_fidl() const {
    return fuchsia::overnet::protocol::StreamId{id_};
  }

 private:
  uint64_t id_;
};

std::ostream& operator<<(std::ostream& out, StreamId stream_id);

inline bool operator==(StreamId a, StreamId b) { return a.get() == b.get(); }
inline bool operator!=(StreamId a, StreamId b) { return !operator==(a, b); }

}  // namespace overnet

namespace std {
template <>
struct hash<overnet::StreamId> {
  size_t operator()(overnet::StreamId id) const { return id.Hash(); }
};
}  // namespace std
