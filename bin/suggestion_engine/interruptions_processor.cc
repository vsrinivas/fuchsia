// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/interruptions_processor.h"
#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace fuchsia {
namespace modular {

InterruptionsProcessor::InterruptionsProcessor() = default;
InterruptionsProcessor::~InterruptionsProcessor() = default;

void InterruptionsProcessor::RegisterListener(
    fidl::InterfaceHandle<InterruptionListener> listener) {
  listeners_.AddInterfacePtr(listener.Bind());
}

void InterruptionsProcessor::SetDecisionPolicy(
    std::unique_ptr<DecisionPolicy> decision_policy) {
  decision_policy_ = std::move(decision_policy);
}

bool InterruptionsProcessor::MaybeInterrupt(
    const RankedSuggestion& suggestion) {
  if (decision_policy_->Accept(suggestion)) {
    for (auto& listener : listeners_.ptrs()) {
      DispatchInterruption(listener->get(), suggestion);
    }
    return true;
  }
  return false;
}

void InterruptionsProcessor::DispatchInterruption(
    InterruptionListener* const listener,
    const RankedSuggestion& ranked_suggestion) {
  Suggestion suggestion = CreateSuggestion(ranked_suggestion);
  suggestion.confidence = kMaxConfidence;
  listener->OnInterrupt(std::move(suggestion));
}

}  // namespace modular
}  // namespace fuchsia
