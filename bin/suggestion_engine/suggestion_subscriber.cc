// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

namespace maxwell {

SuggestionSubscriber::SuggestionSubscriber(
    fidl::InterfaceHandle<SuggestionListener> listener)
    : listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

SuggestionSubscriber::~SuggestionSubscriber() = default;

void SuggestionSubscriber::OnSubscribe() {}

SuggestionPtr SuggestionSubscriber::CreateSuggestion(
    const RankedSuggestion& suggestion_data) {
  auto suggestion = Suggestion::New();
  suggestion->uuid = suggestion_data.prototype->suggestion_id;
  suggestion->confidence = suggestion_data.confidence;
  suggestion->display = suggestion_data.prototype->proposal->display->Clone();
  if (!suggestion_data.prototype->proposal->on_selected.empty()) {
    // TODO(thatguy): Proposal.on_select should be single Action, not an array
    // https://fuchsia.atlassian.net/browse/MW-118
    const auto& selected_action =
        suggestion_data.prototype->proposal->on_selected[0];
    switch (selected_action->which()) {
      case Action::Tag::FOCUS_STORY: {
        suggestion->story_id = selected_action->get_focus_story()->story_id;
        break;
      }
      case Action::Tag::ADD_MODULE_TO_STORY: {
        suggestion->story_id =
            selected_action->get_add_module_to_story()->story_id;
        break;
      }
      default: {}
    }
  }
  return suggestion;
}

}  // namespace maxwell
