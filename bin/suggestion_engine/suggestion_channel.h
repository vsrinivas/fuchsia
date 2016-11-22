// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/agent_suggestion_record.h"

namespace maxwell {
namespace suggestion {

class SuggestionChannel {
 public:
  RankedSuggestion* OnAddSuggestion(SuggestionPrototype* prototype);
  void OnChangeSuggestion(RankedSuggestion* ranked_suggestion);
  void OnRemoveSuggestion(const RankedSuggestion* ranked_suggestion);

  // Returns a read-only mutable vector of suggestions in ranked order, from
  // highest to lowest relevance.
  const std::vector<std::unique_ptr<RankedSuggestion>>* ranked_suggestions()
      const {
    return &ranked_suggestions_;
  }

 protected:
  virtual void DispatchOnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;
  virtual void DispatchOnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;

 private:
  // Current justification: ranking is not implemented; proposals ranked in
  // insertion order.
  //
  // Eventual justification:
  //
  // Use a vector rather than set to allow dynamic reordering. Not all usages
  // take advantage of dynamic reordering, but this is sufficiently general to
  // not require a specialized impl using std::set.
  std::vector<std::unique_ptr<RankedSuggestion>> ranked_suggestions_;
};

template <class Subscriber, class Controller>
class BoundSuggestionChannel : public SuggestionChannel {
 public:
  void AddSubscriber(std::unique_ptr<Subscriber> subscriber) {
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
  maxwell::BindingSet<Controller,
                      std::unique_ptr<Subscriber>,
                      Subscriber::GetBinding>
      subscribers_;
};

}  // namespace suggestion
}  // namespace maxwell
