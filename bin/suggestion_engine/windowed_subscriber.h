// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"
#include "apps/maxwell/src/suggestion_engine/subscriber.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {

// Manages a single Next or Ask suggestion subscriber, translating raw
// suggestion lifecycle events into windowed suggestion lists using a vector of
// ranked suggestions.
class WindowedSubscriber : public Subscriber {
 public:
  typedef std::vector<std::unique_ptr<RankedSuggestion>> RankedSuggestions;

  WindowedSubscriber(const RankedSuggestions* ranked_suggestions,
                     fidl::InterfaceHandle<SuggestionListener> listener)
      : Subscriber(std::move(listener)),
        ranked_suggestions_(ranked_suggestions) {}

  virtual ~WindowedSubscriber() = default;

  void SetResultCount(int32_t count);

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      DispatchAdd(ranked_suggestion);

      // Evict if we were already full
      if (IsFull())
        DispatchRemove(*(*ranked_suggestions_)[max_results_]);
    }
  }

  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      // Shift in if we were full
      if (IsFull())
        DispatchAdd(*(*ranked_suggestions_)[max_results_]);

      DispatchRemove(ranked_suggestion);
    }
  }

  // Notifies the listener that all elements should be updated.
  void Invalidate();

 private:
  bool IsFull() const {
    return ranked_suggestions_->size() > (size_t)max_results_;
  }

  bool IncludeSuggestion(const RankedSuggestion& suggestion) const;

  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  const RankedSuggestions* const ranked_suggestions_;
};

// Convenience template baking a controller interface into WindowedSubscriber.
template <class Controller>
class BoundWindowedSubscriber : public Controller, public WindowedSubscriber {
 public:
  BoundWindowedSubscriber(const RankedSuggestions* ranked_suggestions,
                          fidl::InterfaceHandle<SuggestionListener> listener,
                          fidl::InterfaceRequest<Controller> controller)
      : WindowedSubscriber(ranked_suggestions, std::move(listener)),
        binding_(this, std::move(controller)) {}

  void SetResultCount(int32_t count) override {
    WindowedSubscriber::SetResultCount(count);
  }

 private:
  fidl::Binding<Controller> binding_;
};

}  // namespace maxwell
