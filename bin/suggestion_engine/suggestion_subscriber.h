// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {

struct RankedSuggestion;

class SuggestionSubscriber {
 public:
  SuggestionSubscriber(fidl::InterfaceHandle<SuggestionListener> listener)
      : listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

  virtual ~SuggestionSubscriber() = default;

  virtual void OnAddSuggestion(const RankedSuggestion& ranked_suggestion) = 0;

  virtual void OnRemoveSuggestion(
      const RankedSuggestion& ranked_suggestion) = 0;

  virtual void Invalidate() = 0;

  // FIDL methods, for use with BoundSet without having to expose listener_.

  bool is_bound() { return listener_.is_bound(); }

  void set_connection_error_handler(const fxl::Closure& error_handler) {
    listener_.set_connection_error_handler(error_handler);
  }

  // End FIDL methods.

 protected:
  static SuggestionPtr CreateSuggestion(
      const RankedSuggestion& suggestion_data) {
    auto suggestion = Suggestion::New();
    suggestion->uuid = suggestion_data.prototype->suggestion_id;
    suggestion->rank = suggestion_data.rank;
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

  void DispatchAdd(const RankedSuggestion& ranked_suggestion) {
    fidl::Array<SuggestionPtr> batch;
    batch.push_back(CreateSuggestion(ranked_suggestion));
    listener()->OnAdd(std::move(batch));
  }

  void DispatchRemove(const RankedSuggestion& ranked_suggestion) {
    listener()->OnRemove(ranked_suggestion.prototype->suggestion_id);
  }

  SuggestionListener* listener() const { return listener_.get(); }

 private:
  SuggestionListenerPtr listener_;
};

}  // namespace maxwell
