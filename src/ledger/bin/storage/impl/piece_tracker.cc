// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/piece_tracker.h"

namespace storage {

PieceTracker::PieceTracker() = default;

PieceTracker::ObjectTokenImpl::ObjectTokenImpl(PieceTracker* tracker,
                                               ObjectIdentifier identifier)
    : tracker_(tracker),
      map_entry_(
          tracker_->token_counts_.emplace(std::move(identifier), 0).first) {
  ++map_entry_->second;
}

PieceTracker::ObjectTokenImpl::~ObjectTokenImpl() {
  --map_entry_->second;
  if (map_entry_->second == 0) {
    tracker_->token_counts_.erase(map_entry_);
  }
}

const ObjectIdentifier& PieceTracker::ObjectTokenImpl::GetIdentifier() const {
  return map_entry_->first;
}

PieceTracker::~PieceTracker() { FXL_DCHECK(token_counts_.empty()); }

std::unique_ptr<ObjectToken> PieceTracker::GetObjectToken(
    ObjectIdentifier identifier) {
  // Using `new` to access a non-public constructor.
  return std::unique_ptr<ObjectToken>(
      new ObjectTokenImpl(this, std::move(identifier)));
}

int PieceTracker::count(const ObjectIdentifier& identifier) const {
  auto it = token_counts_.find(identifier);
  if (it == token_counts_.end()) {
    return 0;
  }
  return it->second;
}

int PieceTracker::size() const { return token_counts_.size(); }

}  // namespace storage
