// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_channel.h"
#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

#include <utility>

namespace maxwell {

void SuggestionChannel::AddSubscriber(SuggestionSubscriber* subscriber) {
  subscribers_.push_back(std::move(subscriber));
}

// Methods to dispatch events to all subscribers
void SuggestionChannel::DispatchInvalidate() {
  for (const auto& subscriber : subscribers_) {
    subscriber->Invalidate();
  }
}

void SuggestionChannel::DispatchOnAddSuggestion(RankedSuggestion* suggestion) {
  for (const auto& subscriber : subscribers_) {
    subscriber->OnAddSuggestion(*suggestion);
  }
}

void SuggestionChannel::DispatchOnRemoveSuggestion(
    RankedSuggestion* suggestion) {
  for (const auto& subscriber : subscribers_) {
    subscriber->OnRemoveSuggestion(*suggestion);
  }
}

}  // namespace maxwell
