// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"

namespace maxwell {
namespace suggestion {

class NextChannel : public SuggestionChannel {
 public:
  void AddSubscriber(std::unique_ptr<NextSubscriber> subscriber) {
    subscribers_.emplace(std::move(subscriber));
  }

 protected:
  void DispatchOnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) override {
    for (auto& subscriber : subscribers_)
      subscriber->OnAddSuggestion(ranked_suggestion);
  }

  void DispatchOnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) override {
    for (auto& subscriber : subscribers_)
      subscriber->OnRemoveSuggestion(ranked_suggestion);
  }

 private:
  maxwell::BindingSet<NextController,
                      std::unique_ptr<NextSubscriber>,
                      NextSubscriber::GetBinding>
      subscribers_;
};

}  // namespace suggestion
}  // namespace maxwell
