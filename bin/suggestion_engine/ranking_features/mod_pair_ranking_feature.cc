// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_helper.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/schema.h"

namespace maxwell {

namespace {
constexpr char kDataFilePath[] = "/pkg/data/ranking_data/mod_pairs.json";
}


ModPairRankingFeature::ModPairRankingFeature() {
  InitData();
};

ModPairRankingFeature::~ModPairRankingFeature() = default;

// Sample map using data collected between Feb 6-20, 2018
void ModPairRankingFeature::InitData() {
  std::pair<bool, rapidjson::Document> result = FetchJsonObject(kDataFilePath);
  if (!result.first) {
    FXL_LOG(WARNING) << "Failed to fetch mod pairs ranking feature data.";
    return;
  }
  for (rapidjson::Value::ConstMemberIterator iter = result.second.MemberBegin();
       iter != result.second.MemberEnd(); ++iter) {
    std::string existing_mod_url = iter->name.GetString();
    rapidjson::Value& other_mods = result.second[existing_mod_url.c_str()];
    for (rapidjson::Value::ConstMemberIterator iter2 = other_mods.MemberBegin();
         iter2 != other_mods.MemberEnd(); ++iter2) {
      std::string added_mod_url = iter2->name.GetString();
      module_pairs_[existing_mod_url][added_mod_url] = iter2->value.GetDouble();
    }
  }
}


double ModPairRankingFeature::ComputeFeatureInternal(
    const UserInput& query,
    const RankedSuggestion& suggestion) {
  double prob = 0.0;

  for (auto& action : *suggestion.prototype->proposal->on_selected) {
    f1dl::StringPtr module_url;
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
    for (auto& context_value : *ContextValues()) {
      std::string existing_mod_url = context_value->meta->mod->url;
      if (module_pairs_.count(existing_mod_url) &&
          module_pairs_[existing_mod_url].count(module_url)) {
        prob = std::max(prob, module_pairs_[existing_mod_url][module_url]);
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
