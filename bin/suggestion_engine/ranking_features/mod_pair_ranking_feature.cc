// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_helper.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"

namespace maxwell {

ModPairRankingFeature::ModPairRankingFeature() {
  InitPairProbabilitiesMap();
};

ModPairRankingFeature::~ModPairRankingFeature() = default;

// Sample map using data collected between Feb 6-20, 2018
// TODO(miguelfrde): use up to date data loaded from actual datasource.
void ModPairRankingFeature::InitPairProbabilitiesMap() {
  probabilities_["chat_conversation"]["file:///system/apps/web_view"] = 0.333333333333;
  probabilities_["chat_conversation"]["contacts_picker"] = 0.333333333333;
  probabilities_["chat_conversation"]["gallery"] = 0.333333333333;
  probabilities_["video"]["file:///system/apps/youtube_video"] = 0.5;
  probabilities_["video"]["file:///system/apps/youtube_related_videos"] = 0.5;
  probabilities_["youtube_video"]["video"] = 1.0;
  probabilities_["youtube_story"]["video"] = 0.285714285714;
  probabilities_["youtube_story"]["file:///system/apps/youtube_related_videos"] = 0.357142857143;
  probabilities_["youtube_story"]["file:///system/apps/youtube_video"] = 0.357142857143;
  probabilities_["dashboard"]["chat_conversation"] = 0.5;
  probabilities_["dashboard"]["file:///system/apps/web_view"] = 0.5;
  probabilities_["file:///system/apps/web_view"]["chat_conversation"] = 1.0;
  probabilities_["file:///system/apps/youtube_related_videos"]["video"] = 1.0;
  probabilities_["contacts_picker"]["chat_conversation"] = 1.0;
  probabilities_["file:///system/apps/concert_event_list"]["concert_event_page"] = 1.0;
  probabilities_["gallery"]["chat_conversation"] = 0.5;
  probabilities_["gallery"]["contacts_picker"] = 0.5;
  probabilities_["file:///system/apps/map"]["file:///system/apps/weather_forecast"] = 1.0;
  probabilities_["chat_conversation_list"]["chat_conversation"] = 0.333333333333;
  probabilities_["chat_conversation_list"]["contacts_picker"] = 0.5;
  probabilities_["chat_conversation_list"]["gallery"] = 0.166666666667;
  probabilities_["file:///system/apps/youtube_video"]["video"] = 0.4;
  probabilities_["file:///system/apps/youtube_video"]["file:///system/apps/youtube_related_videos"] = 0.6;
}


double ModPairRankingFeature::ComputeFeatureInternal(
    const UserInput& query,
    const RankedSuggestion& suggestion,
    const f1dl::Array<ContextValuePtr>& context_update_values) {
  double prob = 0.0;

  for (auto& action : suggestion.prototype->proposal->on_selected) {
    f1dl::String module_url;
    switch (action->which()) {
      case Action::Tag::CREATE_STORY: {
        module_url = action->get_create_story()->module_id;
        break;
      }
      case Action::Tag::ADD_MODULE_TO_STORY: {
        module_url = action->get_add_module_to_story()->module_url;
        break;
      }
      case Action::Tag::ADD_MODULE: {
        module_url = action->get_add_module()->daisy->url;
        break;
      }
      case Action::Tag::CUSTOM_ACTION:
      case Action::Tag::FOCUS_STORY:
      case Action::Tag::__UNKNOWN__:
        continue;
    }
    if (module_url.is_null() || module_url->empty()) {
      continue;
    }
    // Currently computing: max{P(m|mi) for mi in modules_in_source_story}
    // TODO(miguelfrde): compute P(module_url | modules in source story)
    for (auto& context_value : context_update_values) {
      std::string existing_mod_url = context_value->meta->mod->url;
      if (probabilities_.count(existing_mod_url) &&
          probabilities_[existing_mod_url].count(module_url)) {
        prob = std::max(prob, probabilities_[existing_mod_url][module_url]);
      }
    }
  }
  return prob;
}

ContextSelectorPtr ModPairRankingFeature::CreateContextSelectorInternal() {
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::MODULE;
  selector->meta = ContextMetadata::New();
  selector->meta->story = StoryMetadata::New();
  selector->meta->story->focused = FocusedState::New();
  selector->meta->story->focused->state = FocusedState::State::FOCUSED;
  return selector;
}

}  // namespace maxwell
