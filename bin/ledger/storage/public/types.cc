// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace {
template <typename T>
std::ostream& operator<<(std::ostream& os, const std::unique_ptr<T>& ptr) {
  if (ptr) {
    return os << *ptr;
  }
  return os;
}

template <typename T>
bool EqualPtr(const std::unique_ptr<T>& lhs, const std::unique_ptr<T>& rhs) {
  if (bool(lhs) != bool(rhs)) {
    return false;
  }
  if (!lhs && !rhs) {
    return true;
  }
  return *lhs == *rhs;
}
}  // namespace

bool operator==(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) {
  return std::tie(lhs.key_index, lhs.deletion_scope_id, lhs.object_digest) ==
         std::tie(rhs.key_index, rhs.deletion_scope_id, rhs.object_digest);
}

bool operator!=(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) {
  return !(lhs == rhs);
}

bool operator<(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) {
  return std::tie(lhs.key_index, lhs.deletion_scope_id, lhs.object_digest) <
         std::tie(rhs.key_index, rhs.deletion_scope_id, rhs.object_digest);
}

bool operator==(const Entry& lhs, const Entry& rhs) {
  return std::tie(lhs.key, lhs.object_digest, lhs.priority) ==
         std::tie(rhs.key, rhs.object_digest, rhs.priority);
}

bool operator!=(const Entry& lhs, const Entry& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Entry& e) {
  return os << "Entry{key: " << e.key << ", value: " << e.object_digest
            << ", priority: "
            << (e.priority == KeyPriority::EAGER ? "EAGER" : "LAZY") << "}";
}

bool operator==(const EntryChange& lhs, const EntryChange& rhs) {
  return lhs.deleted == rhs.deleted &&
         (lhs.deleted ? lhs.entry.key == rhs.entry.key
                      : lhs.entry == rhs.entry);
}

bool operator!=(const EntryChange& lhs, const EntryChange& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const EntryChange& e) {
  return os << "EntryChange{entry: " << e.entry << ", deleted: " << e.deleted
            << "}";
}

bool operator==(const ThreeWayChange& lhs, const ThreeWayChange& rhs) {
  return EqualPtr(lhs.base, rhs.base) && EqualPtr(lhs.left, rhs.left) &&
         EqualPtr(lhs.right, rhs.right);
}

bool operator!=(const ThreeWayChange& lhs, const ThreeWayChange& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const ThreeWayChange& e) {
  return os << "ThreeWayChange{base: " << e.base << ", left: " << e.left
            << ", right: " << e.right << "}";
}

fxl::StringView StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::IO_ERROR:
      return "IO_ERROR";
    case Status::NOT_FOUND:
      return "NOT_FOUND";
    case Status::FORMAT_ERROR:
      return "FORMAT_ERROR";
    case Status::ILLEGAL_STATE:
      return "ILLEGAL_STATE";
    case Status::INTERNAL_IO_ERROR:
      return "INTERNAL_IO_ERROR";
    case Status::INTERRUPTED:
      return "INTERRUPTED";
    case Status::NOT_CONNECTED_ERROR:
      return "NOT_CONNECTED_ERROR";
    case Status::NO_SUCH_CHILD:
      return "NO_SUCH_CHILD";
    case Status::OBJECT_DIGEST_MISMATCH:
      return "OBJECT_DIGEST_MISMATCH";
    case Status::NOT_IMPLEMENTED:
      return "NOT_IMPLEMENTED";
  }
}

std::ostream& operator<<(std::ostream& os, Status status) {
  return os << StatusToString(status);
}

}  // namespace storage
