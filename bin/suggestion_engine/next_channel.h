// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/filter.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"

namespace maxwell {

class NextChannel : public SuggestionChannel {
 public:
  NextChannel(ProposalFilter filter) : filter_(filter) {}

  void AddSubscriber(std::unique_ptr<NextSubscriber> subscriber) {
    subscribers_.emplace(std::move(subscriber));
  }

  RankedSuggestion* OnAddSuggestion(
      const SuggestionPrototype* prototype) override;
  void OnChangeSuggestion(RankedSuggestion* ranked_suggestion) override;
  void OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion) override;
  const RankedSuggestions* ranked_suggestions() const override;

 private:
  void DispatchOnAddSuggestion(const RankedSuggestion& ranked_suggestion) {
    for (auto& subscriber : subscribers_)
      subscriber->OnAddSuggestion(ranked_suggestion);
  }

  void DispatchOnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) {
    for (auto& subscriber : subscribers_)
      subscriber->OnRemoveSuggestion(ranked_suggestion);
  }

  ProposalFilter filter_;

  maxwell::BoundNonMovableSet<NextSubscriber> subscribers_;
  RankedSuggestions ranked_suggestions_;
};

}  // namespace maxwell
