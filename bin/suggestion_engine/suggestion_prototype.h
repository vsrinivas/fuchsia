// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_

#include <strstream>

#include <fuchsia/cpp/modular.h>
#include "lib/fxl/time/time_point.h"

namespace modular {

struct SuggestionPrototype {
  std::string suggestion_id;
  std::string source_url;
  fxl::TimePoint timestamp;
  Proposal proposal;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

// Creates a partial suggestion from a prototype. Confidence will not be set.
Suggestion CreateSuggestion(const SuggestionPrototype& prototype);

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
