// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_

#import <vector>

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace maxwell {

class SuggestionFilter {
 public:
  // Applies the filter to the suggestions in the list, removing any that are
  // deemed irrelevant
  virtual void Filter(std::vector<RankedSuggestion>* suggestions) = 0;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_
