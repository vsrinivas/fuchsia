// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_CHANNEL_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_CHANNEL_H_

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

bool IsInterruption(const SuggestionPrototype& prototype);

class InterruptionsChannel {
 public:
  void AddSubscriber(fidl::InterfaceHandle<SuggestionListener> subscriber,
                     const RankedSuggestions& initial_suggestions_source);
  void AddSuggestion(const SuggestionPrototype& prototype);
  void RemoveSuggestion(const SuggestionPrototype& prototype);

 private:
  fidl::InterfacePtrSet<SuggestionListener> subscribers_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_CHANNEL_H_
