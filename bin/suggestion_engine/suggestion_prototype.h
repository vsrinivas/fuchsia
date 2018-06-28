// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_

#include <strstream>

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fxl/time/time_point.h"

namespace modular {

struct SuggestionPrototype {
  std::string suggestion_id;
  // Story ID is set when the proposal came with a name.
  // fuchsia::modular::SuggestionEngine maps this name namespaced by the source
  // to this ID and propagates it here. If this story id was not set, it can be
  // set to the (deprecated) proposal.story_id.
  std::string story_id;
  std::string source_url;
  fxl::TimePoint timestamp;
  fuchsia::modular::Proposal proposal;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

// Creates a partial suggestion from a prototype. Confidence will not be set.
fuchsia::modular::Suggestion CreateSuggestion(
    const SuggestionPrototype& prototype);

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
