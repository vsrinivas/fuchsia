// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/debug.h"
#include "apps/maxwell/src/suggestion_engine/filter.h"
#include "apps/maxwell/src/suggestion_engine/interruptions_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/next_subscriber.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_channel.h"

namespace maxwell {

class NextChannel : public SuggestionChannel {
 public:
  NextChannel(ProposalFilter filter, SuggestionDebugImpl* debug)
      : debug_(debug), filter_(filter) {}

  void AddSubscriber(std::unique_ptr<NextSubscriber> subscriber) {
    subscribers_.emplace(std::move(subscriber));
  }

  void AddInterruptionsSubscriber(
      std::unique_ptr<InterruptionsSubscriber> subscriber) {
    interruptions_subscribers_.emplace(std::move(subscriber));
  }

  void OnAddSuggestion(SuggestionPrototype* prototype) override;
  void OnChangeSuggestion(RankedSuggestion* ranked_suggestion) override;
  void OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion) override;
  const RankedSuggestions* ranked_suggestions() const override;

 private:
  void DispatchOnAddSuggestion(const RankedSuggestion& ranked_suggestion) {
    debug_->OnNextUpdate(ranked_suggestions());
    for (auto& subscriber : subscribers_)
      subscriber->OnAddSuggestion(ranked_suggestion);
    for (auto& interruptions_subscriber : interruptions_subscribers_)
      interruptions_subscriber->OnAddSuggestion(ranked_suggestion);
  }

  void DispatchOnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) {
    debug_->OnNextUpdate(ranked_suggestions());
    for (auto& subscriber : subscribers_)
      subscriber->OnRemoveSuggestion(ranked_suggestion);
    for (auto& interruptions_subscriber : interruptions_subscribers_)
      interruptions_subscriber->OnRemoveSuggestion(ranked_suggestion);
  }

  SuggestionDebugImpl* debug_;

  ProposalFilter filter_;

  maxwell::BoundNonMovableSet<NextSubscriber> subscribers_;
  maxwell::BoundNonMovableSet<InterruptionsSubscriber>
      interruptions_subscribers_;
  RankedSuggestions ranked_suggestions_;
};

}  // namespace maxwell
