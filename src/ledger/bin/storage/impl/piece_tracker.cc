// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/piece_tracker.h"

#include <sstream>
#include <string>

namespace storage {
namespace {

// Converts a map of ObjectIdentifier counts to a string listing them.
std::string TokenCountsToString(const std::map<ObjectIdentifier, int>& token_counts) {
  std::ostringstream stream;
  for (const auto& token : token_counts) {
    stream << "\n" << token.first << " " << token.second;
  }
  return stream.str();
}

}  // namespace

PieceTracker::PieceTracker() = default;

PieceTracker::PieceTokenImpl::PieceTokenImpl(PieceTracker* tracker, ObjectIdentifier identifier)
    : tracker_(tracker),
      map_entry_(tracker_->token_counts_.emplace(std::move(identifier), 0).first) {
  ++map_entry_->second;
  FXL_VLOG(1) << "PieceToken " << map_entry_->first << " " << map_entry_->second;
}

PieceTracker::PieceTokenImpl::~PieceTokenImpl() {
  --map_entry_->second;
  FXL_VLOG(1) << "PieceToken " << map_entry_->first << " " << map_entry_->second;
  if (map_entry_->second == 0) {
    tracker_->token_counts_.erase(map_entry_);
  }
}

const ObjectIdentifier& PieceTracker::PieceTokenImpl::GetIdentifier() const {
  return map_entry_->first;
}

PieceTracker::~PieceTracker() {
  FXL_DCHECK(token_counts_.empty()) << TokenCountsToString(token_counts_);
}

std::unique_ptr<PieceToken> PieceTracker::GetPieceToken(ObjectIdentifier identifier) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<PieceToken>(new PieceTokenImpl(this, std::move(identifier)));
}

int PieceTracker::count(const ObjectIdentifier& identifier) const {
  auto it = token_counts_.find(identifier);
  if (it == token_counts_.end()) {
    return 0;
  }
  return it->second;
}

int PieceTracker::size() const { return token_counts_.size(); }

DiscardableToken::DiscardableToken(ObjectIdentifier identifier)
    : identifier_(std::move(identifier)) {
  FXL_VLOG(1) << "DiscardableToken " << identifier_;
}

const ObjectIdentifier& DiscardableToken::GetIdentifier() const { return identifier_; }

}  // namespace storage
