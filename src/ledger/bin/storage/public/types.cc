// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/types.h"

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/util/ptr.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {

constexpr absl::string_view kNeverPolicy = "never";
constexpr absl::string_view kEagerPolicy = "eager";
constexpr absl::string_view kRootNodesPolicy = "root_nodes";

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::unique_ptr<T>& ptr) {
  if (ptr) {
    return os << *ptr;
  }
  return os;
}
}  // namespace

ObjectDigest::ObjectDigest() = default;
ObjectDigest::ObjectDigest(std::string digest) : digest_(std::move(digest)) {}
ObjectDigest::ObjectDigest(const flatbuffers::Vector<uint8_t>* digest)
    : ObjectDigest::ObjectDigest(convert::ToString(digest)) {}

ObjectDigest::ObjectDigest(const ObjectDigest&) = default;
ObjectDigest& ObjectDigest::operator=(const ObjectDigest&) = default;
ObjectDigest::ObjectDigest(ObjectDigest&&) noexcept = default;
ObjectDigest& ObjectDigest::operator=(ObjectDigest&&) noexcept = default;

bool ObjectDigest::IsValid() const { return digest_.has_value(); }
const std::string& ObjectDigest::Serialize() const {
  LEDGER_DCHECK(IsValid());
  return digest_.value();
}

bool operator==(const ObjectDigest& lhs, const ObjectDigest& rhs) {
  return lhs.digest_ == rhs.digest_;
}
bool operator!=(const ObjectDigest& lhs, const ObjectDigest& rhs) { return !(lhs == rhs); }
bool operator<(const ObjectDigest& lhs, const ObjectDigest& rhs) {
  return lhs.digest_ < rhs.digest_;
}

std::ostream& operator<<(std::ostream& os, const ObjectDigest& e) {
  return os << (e.IsValid() ? convert::ToHex(e.Serialize()) : "invalid-digest");
}

ObjectIdentifier::ObjectIdentifier()
    : key_index_(0), object_digest_(ObjectDigest()), token_(nullptr) {}

ObjectIdentifier::ObjectIdentifier(uint32_t key_index, ObjectDigest object_digest,
                                   std::shared_ptr<ObjectIdentifier::Token> token)
    : key_index_(key_index), object_digest_(std::move(object_digest)), token_(std::move(token)) {}

ObjectIdentifier::ObjectIdentifier(const ObjectIdentifier&) = default;
ObjectIdentifier& ObjectIdentifier::operator=(const ObjectIdentifier&) = default;
ObjectIdentifier::ObjectIdentifier(ObjectIdentifier&&) noexcept = default;
ObjectIdentifier& ObjectIdentifier::operator=(ObjectIdentifier&&) noexcept = default;

// The destructor must be defined even if purely virtual for destruction to work.
ObjectIdentifier::Token::~Token() = default;

bool operator==(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) {
  return std::tie(lhs.key_index_, lhs.object_digest_) ==
         std::tie(rhs.key_index_, rhs.object_digest_);
}

bool operator!=(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) { return !(lhs == rhs); }

bool operator<(const ObjectIdentifier& lhs, const ObjectIdentifier& rhs) {
  return std::tie(lhs.key_index_, lhs.object_digest_) <
         std::tie(rhs.key_index_, rhs.object_digest_);
}

std::ostream& operator<<(std::ostream& os, const ObjectIdentifier& e) {
  return os << "ObjectIdentifier{key_index: " << e.key_index()
            << ", object_digest: " << e.object_digest() << "}";
}

bool operator==(const Entry& lhs, const Entry& rhs) {
  return std::tie(lhs.key, lhs.object_identifier, lhs.priority, lhs.entry_id) ==
         std::tie(rhs.key, rhs.object_identifier, rhs.priority, rhs.entry_id);
}

bool operator!=(const Entry& lhs, const Entry& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const Entry& e) {
  return os << "Entry{key: " << e.key << ", value: " << e.object_identifier
            << ", priority: " << (e.priority == KeyPriority::EAGER ? "EAGER" : "LAZY")
            << ", entry_id: " << convert::ToHex(e.entry_id) << "}";
}

bool operator==(const EntryChange& lhs, const EntryChange& rhs) {
  return std::tie(lhs.deleted, lhs.entry) == std::tie(rhs.deleted, rhs.entry);
}

bool operator!=(const EntryChange& lhs, const EntryChange& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const EntryChange& e) {
  return os << "EntryChange{entry: " << e.entry << ", deleted: " << e.deleted << "}";
}

bool operator==(const ThreeWayChange& lhs, const ThreeWayChange& rhs) {
  return ledger::EqualPtr(lhs.base, rhs.base) && ledger::EqualPtr(lhs.left, rhs.left) &&
         ledger::EqualPtr(lhs.right, rhs.right);
}

bool operator!=(const ThreeWayChange& lhs, const ThreeWayChange& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const ThreeWayChange& e) {
  return os << "ThreeWayChange{base: " << e.base << ", left: " << e.left << ", right: " << e.right
            << "}";
}

bool operator==(const ClockEntry& lhs, const ClockEntry& rhs) {
  return std::tie(lhs.commit_id, lhs.generation) == std::tie(rhs.commit_id, rhs.generation);
}

bool operator!=(const ClockEntry& lhs, const ClockEntry& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const ClockEntry& e) {
  return os << "ClockEntry{commit_id: " << e.commit_id << ", generation: " << e.generation << "}";
}

bool operator==(const DeviceEntry& lhs, const DeviceEntry& rhs) {
  return std::tie(lhs.head, lhs.cloud) == std::tie(rhs.head, rhs.cloud);
}

bool operator!=(const DeviceEntry& lhs, const DeviceEntry& rhs) { return !(lhs == rhs); }

std::ostream& operator<<(std::ostream& os, const DeviceEntry& e) {
  os << "DeviceEntry{head: " << e.head << ", cloud: ";
  if (e.cloud.has_value()) {
    os << *e.cloud;
  } else {
    os << "<empty>";
  }
  return os << "}";
}

bool AbslParseFlag(absl::string_view text, GarbageCollectionPolicy* policy, std::string* error) {
  if (text == kNeverPolicy) {
    *policy = GarbageCollectionPolicy::NEVER;
    return true;
  }
  if (text == kEagerPolicy) {
    *policy = GarbageCollectionPolicy::EAGER_LIVE_REFERENCES;
    return true;
  }
  if (text == kRootNodesPolicy) {
    *policy = GarbageCollectionPolicy::EAGER_ROOT_NODES;
    return true;
  }
  *error = "unknown value for enumeration";
  return false;
}

std::string AbslUnparseFlag(GarbageCollectionPolicy policy) {
  switch (policy) {
    case GarbageCollectionPolicy::NEVER:
      return convert::ToString(kNeverPolicy);
    case GarbageCollectionPolicy::EAGER_LIVE_REFERENCES:
      return convert::ToString(kEagerPolicy);
    case GarbageCollectionPolicy::EAGER_ROOT_NODES:
      return convert::ToString(kRootNodesPolicy);
    default:
      return absl::StrCat(policy);
  }
}

}  // namespace storage
