// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

#include <vector>

namespace maxwell {

// This class is a side-effect of implementing something that's logically
// pull-based (the Ask workflow) on top of a push-based system. Once Asks
// are entirely pull-based, a windowed subscriber will not be necessary.
//
// WindowedSuggestionSubscribers provide a fixed-size window on top of a
// read-only list of ranked suggestions (RankedSuggestions) -- this view
// contains the top N results, where N can be defined through SetResultCount.
//
// When N is updated, the WindowedSuggestionSubscriber checks to see if its
// window needs to be resized. Let's assume it must be resized, and the
// difference between the current size and the desired size is D:
// 1) If the window is to be shrunk, all listeners are notified with OnRemove
//    events for the D suggestions that are currently in the window,
//    but will not be in the window after resizing.
//
// 2) If the window is to be expanded, and the underlying read-only list is
//    larger than the current window size, then OnAdd events are dispatched
//    to all listeners for every suggestion that is included in the new window.
class WindowedSuggestionSubscriber : public SuggestionSubscriber {
 public:
  WindowedSuggestionSubscriber(
      const RankedSuggestions* ranked_suggestions,
      fidl::InterfaceHandle<SuggestionListener> listener)
      : SuggestionSubscriber(std::move(listener)),
        ranked_suggestions_(ranked_suggestions) {}

  virtual ~WindowedSuggestionSubscriber() = default;

  void SetResultCount(int32_t count);

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      DispatchAdd(ranked_suggestion);

      // Evict if we were already full
      if (IsFull())
        DispatchRemove(*ranked_suggestions_->Get()[max_results_]);
    }
  }

  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) override {
    if (IncludeSuggestion(ranked_suggestion)) {
      // Shift in if we were full
      if (IsFull())
        DispatchAdd(*ranked_suggestions_->Get()[max_results_]);

      DispatchRemove(ranked_suggestion);
    }
  }

  // Notifies the listener that all elements should be updated.
  void Invalidate() override;

 private:
  bool IsFull() const {
    return (ranked_suggestions_->Get()).size() > (size_t)max_results_;
  }

  bool IncludeSuggestion(const RankedSuggestion& suggestion) const;

  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  const RankedSuggestions* ranked_suggestions_;
};

// Convenience template baking a controller interface into
// WindowedSuggestionSubscriber.
template <class Controller>
class BoundWindowedSuggestionSubscriber : public Controller,
                                          public WindowedSuggestionSubscriber {
 public:
  BoundWindowedSuggestionSubscriber(
      const RankedSuggestions* ranked_suggestions,
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
