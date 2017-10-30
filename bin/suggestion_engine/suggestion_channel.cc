// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_channel.h"
#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

#include <utility>

namespace maxwell {

void SuggestionChannel::AddSubscriber(
    std::unique_ptr<SuggestionSubscriber> subscriber) {
  subscriber->OnSubscribe();
  subscribers_.push_back(std::move(subscriber));
}

// Methods to dispatch events to all subscribers
void SuggestionChannel::DispatchInvalidate() {
  for (const auto& subscriber : subscribers_) {
    subscriber->Invalidate();
  }
}

void SuggestionChannel::DispatchOnProcessingChange(bool processing) {
  for (const auto& subscriber : subscribers_) {
    subscriber->OnProcessingChange(processing);
  }
}

void SuggestionChannel::RemoveAllSubscribers() {
  subscribers_.clear();
}

}  // namespace maxwell
