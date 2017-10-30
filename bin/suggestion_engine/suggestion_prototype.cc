// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace maxwell {

std::string short_proposal_str(const SuggestionPrototype& prototype) {
  std::ostringstream str;
  str << "proposal " << prototype.proposal->id << " from "
      << prototype.source_url;
  return str.str();
}

SuggestionPtr CreateSuggestion(const SuggestionPrototype& prototype) {
  auto suggestion = Suggestion::New();
  suggestion->uuid = prototype.suggestion_id;
  suggestion->display = prototype.proposal->display->Clone();
  if (!prototype.proposal->on_selected.empty()) {
    // TODO(thatguy): Proposal.on_select should be single Action, not an array
    // https://fuchsia.atlassian.net/browse/MW-118
    const auto& selected_action = prototype.proposal->on_selected[0];
    switch (selected_action->which()) {
      case Action::Tag::FOCUS_STORY:
        suggestion->story_id = selected_action->get_focus_story()->story_id;
        break;
      case Action::Tag::ADD_MODULE_TO_STORY:
        suggestion->story_id =
            selected_action->get_add_module_to_story()->story_id;
        break;
      default:
        break;
    }
  }
  return suggestion;
}

}  // namespace maxwell
