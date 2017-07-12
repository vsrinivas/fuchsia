// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestions.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_subscriber.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
class InterruptionsSubscriber : public SuggestionSubscriber {
 public:
  InterruptionsSubscriber(fidl::InterfaceHandle<SuggestionListener> listener)
      : SuggestionSubscriber(std::move(listener)) {}

  virtual void OnAddSuggestion(
      const RankedSuggestion& ranked_suggestion) override {
    if (ranked_suggestion.prototype->proposal->display->annoyance !=
        AnnoyanceType::NONE) {
      DispatchAdd(ranked_suggestion);
    }
  }

  virtual void OnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) override {
    if (ranked_suggestion.prototype->proposal->display->annoyance !=
        AnnoyanceType::NONE) {
      DispatchRemove(ranked_suggestion);
    }
  }
};
}  // namespace maxwell
