// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTION_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTION_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace modular {

// |rank| and |adjusted_confidence| should satisfy the invariant that for any
// sorted set of ranked suggestions, |rank| is increasing and
// |adjusted_confidence| is nonincreasing.
struct RankedSuggestion {
  // Metadata of the suggestion.
  SuggestionPrototype* prototype;

  // Confidence of the suggestion. Updated during ranking.
  double confidence;

  // Whether or not the suggestion should be hidden (filtered by passive
  // filters) in the list and not returned when fetching next suggestions.
  bool hidden;

  // Whether or not the suggesiton is currently being used as interruption. It
  // should be filtered from the list that returns next suggestions.
  bool interrupting;

  static std::unique_ptr<RankedSuggestion> New(SuggestionPrototype* prototype);
};

fuchsia::modular::Suggestion CreateSuggestion(
    const RankedSuggestion& suggestion_data);

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTION_H_
