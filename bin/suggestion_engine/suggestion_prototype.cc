// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <sstream>

#include "lib/fidl/cpp/clone.h"

namespace modular {

std::string short_proposal_str(const SuggestionPrototype& prototype) {
  std::ostringstream str;
  str << "proposal " << prototype.proposal.id << " from "
      << prototype.source_url;
  return str.str();
}

Suggestion CreateSuggestion(const SuggestionPrototype& prototype) {
  Suggestion suggestion;
  suggestion.uuid = prototype.suggestion_id;
  fidl::Clone(prototype.proposal.display, &suggestion.display);
  if (!prototype.proposal.on_selected->empty()) {
    // TODO(thatguy): Proposal.on_select should be single Action, not an array
    // https://fuchsia.atlassian.net/browse/MW-118
    const auto& selected_action = prototype.proposal.on_selected->at(0);
    switch (selected_action.Which()) {
      case Action::Tag::kFocusStory:
        suggestion.story_id = selected_action.focus_story().story_id;
        break;
      case Action::Tag::kAddModuleToStory:
        suggestion.story_id =
            selected_action.add_module_to_story().story_id;
        break;
      default:
        break;
    }
  }
  return suggestion;
}

}  // namespace modular
