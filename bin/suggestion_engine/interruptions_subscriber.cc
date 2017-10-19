// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/interruptions_subscriber.h"

namespace maxwell {

InterruptionsSubscriber::InterruptionsSubscriber(
    fidl::InterfaceHandle<SuggestionListener> listener)
    : SuggestionSubscriber(std::move(listener)) {}

InterruptionsSubscriber::~InterruptionsSubscriber() = default;

void InterruptionsSubscriber::OnAddSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  if (ranked_suggestion.prototype->proposal->display->annoyance !=
      AnnoyanceType::NONE) {
    DispatchAdd(ranked_suggestion);
  }
}

void InterruptionsSubscriber::OnRemoveSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  if (ranked_suggestion.prototype->proposal->display->annoyance !=
      AnnoyanceType::NONE) {
    DispatchRemove(ranked_suggestion);
  }
}

void InterruptionsSubscriber::Invalidate() {
  return;
}

// TODO(jwnichols): Remove this when we give interruptions their own
// interruption-specific listener instead of reusing SuggestionListener
void InterruptionsSubscriber::OnProcessingChange(bool processing) {
  DispatchProcessingChange(processing);
}

}  // namespace maxwell
