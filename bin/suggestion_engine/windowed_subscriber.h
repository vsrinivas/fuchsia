// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion {

// Manages a single Next or Ask suggestion subscriber, translating raw
// suggestion lifecycle events into windowed suggestion lists using a vector of
// ranked suggestions.
class WindowedSubscriber {
 public:
  typedef std::vector<std::unique_ptr<RankedSuggestion>> RankedSuggestions;

  WindowedSubscriber(fidl::InterfaceHandle<Listener> listener)
      : listener_(ListenerPtr::Create(std::move(listener))) {}

  // Lazy initializer for ranked suggestions. This method must be called before
  // any substantial methods on this class are called. Ideally it would be part
  // of construction, but it is kept lazy to facilitate subscriber impls that
  // own their own channels.
  void SetRankedSuggestions(const RankedSuggestions* ranked_suggestions) {
    ranked_suggestions_ = ranked_suggestions;
  }

  void SetResultCount(int32_t count);

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) {
    if (IncludeSuggestion(ranked_suggestion)) {
      DispatchAdd(ranked_suggestion);

      // Evict if we were already full
      if (IsFull())
        DispatchRemove(*(*ranked_suggestions_)[max_results_]);
    }
  }

  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) {
    if (IncludeSuggestion(ranked_suggestion)) {
      // Shift in if we were full
      if (IsFull())
        DispatchAdd(*(*ranked_suggestions_)[max_results_]);

      DispatchRemove(ranked_suggestion);
    }
  }

 private:
  static SuggestionPtr CreateSuggestion(
      const RankedSuggestion& suggestion_data) {
    auto suggestion = Suggestion::New();
    suggestion->uuid = suggestion_data.prototype->first;
    suggestion->rank = suggestion_data.rank;
    suggestion->display =
        suggestion_data.prototype->second->proposal->display->Clone();
    return suggestion;
  }

  bool IsFull() const {
    return ranked_suggestions_->size() > (size_t)max_results_;
  }

  void DispatchAdd(const RankedSuggestion& ranked_suggestion) {
    fidl::Array<SuggestionPtr> batch;
    batch.push_back(CreateSuggestion(ranked_suggestion));
    listener_->OnAdd(std::move(batch));
  }

  void DispatchRemove(const RankedSuggestion& ranked_suggestion) {
    listener_->OnRemove(ranked_suggestion.prototype->first);
  }

  bool IncludeSuggestion(const RankedSuggestion& suggestion) const;

  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  const RankedSuggestions* ranked_suggestions_;
  ListenerPtr listener_;
};

// Convenience template baking a controller interface into WindowedSubscriber.
template <class Controller>
class BoundWindowedSubscriber : public Controller, public WindowedSubscriber {
 public:
  template <class Impl = BoundWindowedSubscriber>
  static fidl::Binding<Controller>* GetBinding(
      std::unique_ptr<Impl>* subscriber) {
    return &(*subscriber)->binding_;
  }

  BoundWindowedSubscriber(fidl::InterfaceHandle<Listener> listener)
      : WindowedSubscriber(std::move(listener)), binding_(this) {}

  void Bind(fidl::InterfaceRequest<Controller> request) {
    binding_.Bind(std::move(request));
  }

  void SetResultCount(int32_t count) override {
    WindowedSubscriber::SetResultCount(count);
  }

 private:
  fidl::Binding<Controller> binding_;
};

}  // namespace suggestion
}  // namespace maxwell
