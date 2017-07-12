// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestions.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_prototype.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_subscriber.h"
#include "lib/fidl/cpp/bindings/binding.h"

#include <vector>

namespace maxwell {

// Manages a single Next or Ask suggestion subscriber, translating raw
// suggestion lifecycle events into windowed suggestion lists using a vector of
// ranked suggestions.
class WindowedSuggestionSubscriber : public SuggestionSubscriber {
 public:
  WindowedSuggestionSubscriber(
      RankedSuggestions* ranked_suggestions,
      fidl::InterfaceHandle<SuggestionListener> listener)
      : SuggestionSubscriber(std::move(listener)),
        ranked_suggestions_(*(ranked_suggestions->GetSuggestions())) {}

  virtual ~WindowedSuggestionSubscriber() = default;

  void SetResultCount(int32_t count);

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      DispatchAdd(ranked_suggestion);

      // Evict if we were already full
      if (IsFull())
        DispatchRemove(*ranked_suggestions_[max_results_]);
    }
  }

  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      // Shift in if we were full
      if (IsFull())
        DispatchAdd(*ranked_suggestions_[max_results_]);

      DispatchRemove(ranked_suggestion);
    }
  }

  // Notifies the listener that all elements should be updated.
  void Invalidate() override;

 private:
  bool IsFull() const {
    return ranked_suggestions_.size() > (size_t)max_results_;
  }

  bool IncludeSuggestion(const RankedSuggestion& suggestion) const;

  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  const std::vector<RankedSuggestion*>& ranked_suggestions_;
};

// Convenience template baking a controller interface into
// WindowedSuggestionSubscriber.
template <class Controller>
class BoundWindowedSuggestionSubscriber : public Controller,
                                          public WindowedSuggestionSubscriber {
 public:
  BoundWindowedSuggestionSubscriber(
      RankedSuggestions* ranked_suggestions,
      fidl::InterfaceHandle<SuggestionListener> listener,
      fidl::InterfaceRequest<Controller> controller)
      : WindowedSuggestionSubscriber(std::move(ranked_suggestions),
                                     std::move(listener)),
        binding_(this, std::move(controller)) {}

  void SetResultCount(int32_t count) override {
    WindowedSuggestionSubscriber::SetResultCount(count);
  }

 private:
  fidl::Binding<Controller> binding_;
};

}  // namespace maxwell
