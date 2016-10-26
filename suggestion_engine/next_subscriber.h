// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion_engine {

// Manages a single "Next" suggestion subscriber, translating raw suggestion
// lifecycle events into windowed suggestion lists.
//
// TODO(rosswang): Ask is probably the more general case, but we probably want
// a direct propagation channel for agents to be sensitive to Asks (as well as
// an indirect context channel to catch agents that weren't engineered for Ask).
class NextSubscriber : public NextController {
 public:
  static mojo::Binding<NextController>* GetBinding(
      std::unique_ptr<NextSubscriber>* next_subscriber) {
    return &(*next_subscriber)->binding_;
  }

  NextSubscriber(std::vector<Suggestion*>* ranked_suggestions,
                 mojo::InterfaceHandle<SuggestionListener> listener)
      : binding_(this),
        ranked_suggestions_(ranked_suggestions),
        listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

  void Bind(mojo::InterfaceRequest<NextController> request) {
    binding_.Bind(std::move(request));
  }

  void SetResultCount(int32_t count) override;

  void OnNewSuggestion(const Suggestion& suggestion) {
    if (IncludeSuggestion(suggestion)) {
      mojo::Array<SuggestionPtr> batch;
      batch.push_back(suggestion.Clone());
      listener_->OnAdd(std::move(batch));
    }
  }

  void BeforeRemoveSuggestion(const Suggestion& suggestion) {
    if (IncludeSuggestion(suggestion)) {
      listener_->OnRemove(suggestion.uuid);
    }
  }

 private:
  bool IncludeSuggestion(const Suggestion& suggestion) const;

  mojo::Binding<NextController> binding_;
  // An upper bound on the number of suggestions to offer this subscriber, as
  // given by SetResultCount.
  int32_t max_results_ = 0;
  std::vector<Suggestion*>* ranked_suggestions_;
  SuggestionListenerPtr listener_;
};

}  // namespace suggestion_engine
}  // namespace maxwell
