// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_

#include <strstream>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/zx/time.h>

namespace modular {

struct SuggestionPrototype {
  std::string suggestion_id;
  std::string preloaded_story_id;
  std::string source_url;
  zx::time timestamp;
  fuchsia::modular::Proposal proposal;
  fidl::InterfacePtr<fuchsia::modular::ProposalListener> bound_listener;
};

std::string short_proposal_str(const SuggestionPrototype& prototype);

// Creates a partial suggestion from a prototype. Confidence will not be set.
fuchsia::modular::Suggestion CreateSuggestion(
    const SuggestionPrototype& prototype);

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_PROTOTYPE_H_
