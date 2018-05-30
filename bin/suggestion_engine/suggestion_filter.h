// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_

#include <vector>

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"

namespace fuchsia {
namespace modular {

class SuggestionFilter {
 public:
  SuggestionFilter();
  virtual ~SuggestionFilter();

  // This function can have the following two implementations:
  //  a) Browse through the vector of ranked suggestions and remove/alter the suggestions
  //     that meet a certain criteria.
  //  b) Evaluate the suggestion based on the filter criteria and return a boolean value.
  // TODO(miguelfrde): improve interface and filtering.
  virtual bool Filter(
      const std::unique_ptr<RankedSuggestion>& suggestion,
      std::vector<std::unique_ptr<RankedSuggestion>>* suggestions) = 0;
};

}  // namespace modular
}  // namespace fuchsia

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_FILTER_H_
