// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/piece_tracker.h"

#include <sstream>
#include <string>

namespace storage {
namespace {

// Converts a map of ObjectIdentifier counts to a string listing them.
std::string TokenCountsToString(
    const std::map<ObjectIdentifier, std::weak_ptr<PieceToken>>& tokens) {
  std::ostringstream stream;
  for (const auto& token : tokens) {
    stream << "\n" << token.first << " " << token.second.use_count();
  }
  return stream.str();
}

}  // namespace

// PieceToken implementation that cleans up its entry in the the token map upon destruction.
class PieceTracker::PieceTokenImpl : public PieceToken {
 public:
  explicit PieceTokenImpl(PieceTracker* tracker,
                          std::map<ObjectIdentifier, std::weak_ptr<PieceToken>>::iterator map_entry)
      : tracker_(tracker), map_entry_(map_entry) {
    FXL_VLOG(1) << "PieceToken: start tracking " << map_entry_->first;
  }

  ~PieceTokenImpl() override {
    FXL_VLOG(1) << "PieceToken: stop tracking " << map_entry_->first;
    FXL_DCHECK(tracker_->thread_checker_.IsCreationThreadCurrent());
    FXL_DCHECK(tracker_->dispatcher_checker_.IsCreationDispatcherCurrent());
    FXL_DCHECK(map_entry_->second.expired());

    tracker_->tokens_.erase(map_entry_);
  }

  const ObjectIdentifier& GetIdentifier() const override { return map_entry_->first; }

 private:
  PieceTracker* tracker_;
  std::map<ObjectIdentifier, std::weak_ptr<PieceToken>>::iterator map_entry_;
};

PieceTracker::PieceTracker() {}

PieceTracker::~PieceTracker() { FXL_DCHECK(tokens_.empty()) << TokenCountsToString(tokens_); }

std::shared_ptr<PieceToken> PieceTracker::GetPieceToken(ObjectIdentifier identifier) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(dispatcher_checker_.IsCreationDispatcherCurrent());

  auto [it, created] = tokens_.try_emplace(std::move(identifier));
  if (!created) {
    FXL_DCHECK(!it->second.expired());
    return it->second.lock();
  }
  auto token = std::make_shared<PieceTokenImpl>(this, it);
  it->second = token;
  FXL_DCHECK(it->second.use_count() == 1);
  return token;
}

int PieceTracker::count(const ObjectIdentifier& identifier) const {
  auto it = tokens_.find(identifier);
  if (it == tokens_.end()) {
    return 0;
  }
  return it->second.use_count();
}

int PieceTracker::size() const { return tokens_.size(); }

DiscardableToken::DiscardableToken(ObjectIdentifier identifier)
    : identifier_(std::move(identifier)) {
  FXL_VLOG(1) << "DiscardableToken " << identifier_;
}

const ObjectIdentifier& DiscardableToken::GetIdentifier() const { return identifier_; }

}  // namespace storage
