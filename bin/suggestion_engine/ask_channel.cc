// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"

#include "apps/maxwell/src/suggestion_engine/ask_subscriber.h"

namespace maxwell {
namespace suggestion {

void AskChannel::DispatchOnAddSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  subscriber_->OnAddSuggestion(ranked_suggestion);
}

void AskChannel::DispatchOnRemoveSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  subscriber_->OnRemoveSuggestion(ranked_suggestion);
}

}  // namespace suggestion
}  // namespace maxwell
