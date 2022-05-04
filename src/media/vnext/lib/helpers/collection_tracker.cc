// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/collection_tracker.h"

namespace fmlib {

void CollectionEntryTracker::OnAdded() {
  switch (state_) {
    case State::kAbsent:
      state_ = State::kAdded;
      break;
    case State::kRemoved:
      state_ = State::kUpdated;
      break;
    case State::kPresent:
    case State::kUpdated:
    case State::kAdded:
      FX_CHECK(false) << "OnAdded called for existing item";
      break;
  }
}

void CollectionEntryTracker::OnUpdated() {
  switch (state_) {
    case State::kAbsent:
    case State::kRemoved:
      FX_CHECK(false) << "OnUpdated called for non-existent item";
      break;
    case State::kPresent:
    case State::kUpdated:
      state_ = State::kUpdated;
      break;
    case State::kAdded:
      // no change
      break;
  }
}

void CollectionEntryTracker::OnRemoved() {
  switch (state_) {
    case State::kAbsent:
    case State::kRemoved:
      FX_CHECK(false) << "OnRemoved called for non-existent item";
      break;
    case State::kPresent:
    case State::kUpdated:
      state_ = State::kRemoved;
      break;
    case State::kAdded:
      state_ = State::kAbsent;
      break;
  }
}

CleanAction CollectionEntryTracker::Clean() {
  switch (state_) {
    case State::kAbsent:
    case State::kPresent:
      // no change
      return CleanAction::kNone;
    case State::kRemoved:
      state_ = State::kAbsent;
      return CleanAction::kRemove;
    case State::kUpdated:
      state_ = State::kPresent;
      return CleanAction::kUpdate;
    case State::kAdded:
      state_ = State::kPresent;
      return CleanAction::kAdd;
  }
}

bool CollectionEntryTracker::is_dirty() const {
  switch (state_) {
    case State::kAbsent:
    case State::kPresent:
      return false;
    case State::kRemoved:
    case State::kUpdated:
    case State::kAdded:
      return true;
  }
}

}  // namespace fmlib
