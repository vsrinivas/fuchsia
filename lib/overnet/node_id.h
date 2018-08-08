// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>
#include <iosfwd>
#include "serialization_helpers.h"
#include "status.h"

namespace overnet {

// Address of a node on the overlay network. This is intended to be relatively
// random and unguessable.
class NodeId {
 public:
  explicit NodeId(uint64_t id) : id_(id) {}
  bool operator==(NodeId other) const { return id_ == other.id_; }
  bool operator!=(NodeId other) const { return id_ != other.id_; }
  bool operator<(NodeId other) const { return id_ < other.id_; }

  uint64_t Hash() const { return id_; }
  uint64_t get() const { return id_; }
  std::string ToString() const;
  static StatusOr<NodeId> FromString(const std::string& s);

  static size_t wire_length() { return sizeof(id_); }
  uint8_t* Write(uint8_t* dst) const { return WriteLE64(id_, dst); }

 private:
  uint64_t id_;
};

std::ostream& operator<<(std::ostream& out, NodeId node_id);

}  // namespace overnet

namespace std {
template <>
struct hash<overnet::NodeId> {
  size_t operator()(const overnet::NodeId& id) const { return id.Hash(); }
};
}  // namespace std
