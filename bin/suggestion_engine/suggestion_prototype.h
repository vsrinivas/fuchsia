// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_

#include <strstream>

#include "lib/fxl/time/time_point.h"
#include "lib/suggestion/fidl/proposal.fidl.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"

namespace maxwell {

struct SuggestionPrototype {
  std::string suggestion_id;
  std::string source_url;
  fxl::TimePoint timestamp;
  ProposalPtr proposal;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

// Creates a partial suggestion from a prototype. Confidence will not be set.
SuggestionPtr CreateSuggestion(const SuggestionPrototype& prototype);

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
