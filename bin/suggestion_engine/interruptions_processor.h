// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "peridot/bin/suggestion_engine/decision_policies/decision_policy.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace modular {

// The InterruptionProcessor determines whether a proposal should interrupt
// the user. If the decision to interrupt is made, this processor also
// determines when and how the interruption should occur.
//
// All interrupting suggestions remain stored as contextual "next"
// suggestions.
class InterruptionsProcessor {
 public:
  InterruptionsProcessor();
  ~InterruptionsProcessor();

  // Ranker that will be used to know if a suggestion should interrupt.
  void SetDecisionPolicy(std::unique_ptr<DecisionPolicy> ranker);

  // Add listener that will be notified when an interruption comes.
  void RegisterListener(
      fidl::InterfaceHandle<fuchsia::modular::InterruptionListener> listener);

  // Based on ranker confidence, dispatch an interruption for the given
  // suggestion
  bool MaybeInterrupt(const RankedSuggestion& suggestion);

 private:
  void DispatchInterruption(
      fuchsia::modular::InterruptionListener* const listener,
      const RankedSuggestion& ranked_suggestion);

  fidl::InterfacePtrSet<fuchsia::modular::InterruptionListener> listeners_;

  std::unique_ptr<DecisionPolicy> decision_policy_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_
