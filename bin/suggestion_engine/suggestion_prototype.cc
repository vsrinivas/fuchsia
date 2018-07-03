// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <sstream>

#include <lib/fidl/cpp/clone.h>

namespace modular {

std::string short_proposal_str(const SuggestionPrototype& prototype) {
  std::ostringstream str;
  str << "proposal " << prototype.proposal.id << " from "
      << prototype.source_url;
  return str.str();
}

fuchsia::modular::Suggestion CreateSuggestion(
    const SuggestionPrototype& prototype) {
  fuchsia::modular::Suggestion suggestion;
  suggestion.uuid = prototype.suggestion_id;
  if (!prototype.preloaded_story_id.empty()) {
    suggestion.preloaded_story_id = prototype.preloaded_story_id;
  }
  fidl::Clone(prototype.proposal.display, &suggestion.display);
  return suggestion;
}

}  // namespace modular
