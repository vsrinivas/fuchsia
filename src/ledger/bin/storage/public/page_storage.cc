// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/page_storage.h"

#include "src/ledger/lib/logging/logging.h"

namespace storage {

PageStorage::CommitIdAndBytes::CommitIdAndBytes() = default;

PageStorage::CommitIdAndBytes::CommitIdAndBytes(CommitId id, std::string bytes)
    : id(std::move(id)), bytes(std::move(bytes)) {}

PageStorage::CommitIdAndBytes::CommitIdAndBytes(CommitIdAndBytes&& other) noexcept = default;

PageStorage::CommitIdAndBytes& PageStorage::CommitIdAndBytes::operator=(
    CommitIdAndBytes&& other) noexcept = default;

bool operator==(const PageStorage::CommitIdAndBytes& lhs,
                const PageStorage::CommitIdAndBytes& rhs) {
  return std::tie(lhs.id, lhs.bytes) == std::tie(rhs.id, rhs.bytes);
}

PageStorage::Location::Location() : tag_(Tag::LOCAL) {}

PageStorage::Location PageStorage::Location::Local() { return Location(Tag::LOCAL, ""); }

PageStorage::Location PageStorage::Location::ValueFromNetwork() {
  return Location(Tag::NETWORK_VALUE, "");
}

PageStorage::Location PageStorage::Location::TreeNodeFromNetwork(std::string in_commit) {
  return Location(Tag::NETWORK_TREE_NODE, std::move(in_commit));
}

bool PageStorage::Location::is_local() const { return tag_ == Tag::LOCAL; }

bool PageStorage::Location::is_value_from_network() const { return tag_ == Tag::NETWORK_VALUE; }

bool PageStorage::Location::is_tree_node_from_network() const {
  return tag_ == Tag::NETWORK_TREE_NODE;
}

bool PageStorage::Location::is_network() const {
  return is_value_from_network() || is_tree_node_from_network();
}

const CommitId& PageStorage::Location::in_commit() const {
  LEDGER_DCHECK(is_tree_node_from_network());
  return in_commit_;
}

PageStorage::Location::Location(Tag tag, CommitId in_commit)
    : tag_(tag), in_commit_(std::move(in_commit)){};

bool operator==(const PageStorage::Location& lhs, const PageStorage::Location& rhs) {
  return std::tie(lhs.tag_, lhs.in_commit_) == std::tie(rhs.tag_, rhs.in_commit_);
}

bool operator!=(const PageStorage::Location& lhs, const PageStorage::Location& rhs) {
  return !(lhs == rhs);
}

bool operator<(const PageStorage::Location& lhs, const PageStorage::Location& rhs) {
  return std::tie(lhs.tag_, lhs.in_commit_) < std::tie(rhs.tag_, rhs.in_commit_);
}

}  // namespace storage
