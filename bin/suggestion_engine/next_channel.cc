// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/next_channel.h"

namespace maxwell {
namespace suggestion {

void NextChannel::DispatchOnAddSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  for (auto& subscriber : subscribers_)
    subscriber->OnAddSuggestion(ranked_suggestion);
}

void NextChannel::DispatchOnRemoveSuggestion(
    const RankedSuggestion& ranked_suggestion) {
  for (auto& subscriber : subscribers_)
    subscriber->OnRemoveSuggestion(ranked_suggestion);
}

}  // namespace suggestion
}  // namespace maxwell
