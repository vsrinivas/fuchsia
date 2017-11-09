// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

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

  void RegisterListener(fidl::InterfaceHandle<InterruptionListener> listener);
  bool ConsiderSuggestion(const SuggestionPrototype& prototype);

 private:
  bool IsInterruption(const SuggestionPrototype& prototype);
  void DispatchInterruption(InterruptionListener* const listener,
                            const SuggestionPrototype& prototype);

  fidl::InterfacePtrSet<InterruptionListener> listeners_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_PROCESSOR_H_
