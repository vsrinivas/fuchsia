// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"

#include "src/lib/uuid/uuid.h"

namespace modular {

SuggestionPrototype* CreateSuggestionPrototype(
    SuggestionPrototypeMap* owner, const std::string& source_url,
    fuchsia::modular::Proposal proposal) {
  return CreateSuggestionPrototype(owner, source_url, "", std::move(proposal));
}

SuggestionPrototype* CreateSuggestionPrototype(
    SuggestionPrototypeMap* owner, const std::string& source_url,
    const std::string& preloaded_story_id,
    fuchsia::modular::Proposal proposal) {
  auto prototype_pair = owner->emplace(std::make_pair(source_url, proposal.id),
                                       std::make_unique<SuggestionPrototype>());

  SuggestionPrototype* suggestion_prototype =
      prototype_pair.first->second.get();
  suggestion_prototype->preloaded_story_id = preloaded_story_id;
  suggestion_prototype->suggestion_id = uuid::Generate();
  suggestion_prototype->source_url = source_url;
  suggestion_prototype->timestamp = zx::clock::get_monotonic();
  suggestion_prototype->proposal = std::move(proposal);

  return suggestion_prototype;
}

}  // namespace modular
