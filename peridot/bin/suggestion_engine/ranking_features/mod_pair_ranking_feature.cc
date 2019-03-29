// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"

#include <lib/context/cpp/context_helper.h>
#include <src/lib/fxl/logging.h>

#include "rapidjson/document.h"
#include "rapidjson/schema.h"

namespace modular {

namespace {
// Sample map using data collected between Feb 6-20, 2018
constexpr char kDataFilePath[] = "/pkg/data/ranking_data/mod_pairs.json";
}  // namespace

ModPairRankingFeature::ModPairRankingFeature(const bool init_data) {
  if (init_data) {
    LoadDataFromFile(kDataFilePath);
  }
}

ModPairRankingFeature::~ModPairRankingFeature() = default;

void ModPairRankingFeature::LoadDataFromFile(const std::string& filepath) {
  auto maybe_result = FetchJsonObject(filepath);
  if (!maybe_result.has_value()) {
    FXL_LOG(WARNING) << "Failed to fetch mod pairs ranking feature data.";
    return;
  }
  auto& result = maybe_result.value();
  module_pairs_.clear();
  for (rapidjson::Value::ConstMemberIterator iter = result.MemberBegin();
       iter != result.MemberEnd(); ++iter) {
    const std::string existing_mod_url = iter->name.GetString();
    rapidjson::Value& other_mods = result[existing_mod_url.c_str()];
    for (rapidjson::Value::ConstMemberIterator iter2 = other_mods.MemberBegin();
         iter2 != other_mods.MemberEnd(); ++iter2) {
      const std::string added_mod_url = iter2->name.GetString();
      module_pairs_[existing_mod_url][added_mod_url] = iter2->value.GetDouble();
    }
  }
}

double ModPairRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& suggestion) {
  double prob = 0.0;

  for (auto& command : suggestion.prototype->proposal.on_selected) {
    fidl::StringPtr module_url;
    switch (command.Which()) {
      case fuchsia::modular::StoryCommand::Tag::kAddMod: {
        module_url = command.add_mod().intent.handler;
        break;
      }
      case fuchsia::modular::StoryCommand::Tag::kSetKindOfProtoStoryOption:
      case fuchsia::modular::StoryCommand::Tag::kSetFocusState:
      case fuchsia::modular::StoryCommand::Tag::kFocusMod:
      case fuchsia::modular::StoryCommand::Tag::kSetLinkValue:
      case fuchsia::modular::StoryCommand::Tag::kRemoveMod:
      case fuchsia::modular::StoryCommand::Tag::Invalid:
        continue;
    }
    if (module_url.is_null() || module_url->empty()) {
      continue;
    }
    // Currently computing: max{P(m|mi) for mi in modules_in_source_story}
    // TODO(miguelfrde): compute P(module_url | modules in source story)
    for (auto& context_value : *ContextValues()) {
      const std::string existing_mod_url = context_value.meta.mod->url;
      if (module_pairs_.count(existing_mod_url) &&
          module_pairs_[existing_mod_url].count(module_url)) {
        prob = std::max(prob, module_pairs_[existing_mod_url][module_url]);
      }
    }
  }
  return prob;
}

fuchsia::modular::ContextSelectorPtr
ModPairRankingFeature::CreateContextSelectorInternal() {
  // Get modules in the currently focused story.
  auto selector = fuchsia::modular::ContextSelector::New();
  selector->type = fuchsia::modular::ContextValueType::MODULE;
  selector->meta = fuchsia::modular::ContextMetadata::New();
  selector->meta->story = fuchsia::modular::StoryMetadata::New();
  selector->meta->story->focused = fuchsia::modular::FocusedState::New();
  selector->meta->story->focused->state =
      fuchsia::modular::FocusedStateState::FOCUSED;
  return selector;
}

}  // namespace modular
