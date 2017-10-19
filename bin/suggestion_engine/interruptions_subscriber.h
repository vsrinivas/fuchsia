// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_SUBSCRIBER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_SUBSCRIBER_H_

#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestion.h"
#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

namespace maxwell {

class InterruptionsSubscriber : public SuggestionSubscriber {
 public:
  InterruptionsSubscriber(fidl::InterfaceHandle<SuggestionListener> listener);
  ~InterruptionsSubscriber() override;

  void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) override;
  void OnRemoveSuggestion(const RankedSuggestion& ranked_suggestion) override;
  void Invalidate() override;
  void OnProcessingChange(bool processing) override;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_INTERRUPTIONS_SUBSCRIBER_H_
