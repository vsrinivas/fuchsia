// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <sstream>

#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/random/uuid.h"

namespace modular {

SuggestionPrototype::~SuggestionPrototype() = default;

SuggestionPrototype::SuggestionPrototype(
    std::string source_url,
    std::string story_id,
    fuchsia::modular::Proposal proposal)
    : suggestion_id(fxl::GenerateUUID()),
      timestamp(fxl::TimePoint::Now()),
      story_id(story_id.empty() ? proposal.story_id.get()
                                : std::move(story_id)),
      source_url(std::move(source_url)),
      proposal(std::move(proposal)) {}

SuggestionPrototype* SuggestionPrototype::CreateInMap(
    SuggestionPrototypeMap* owner, const std::string& source_url,
    const std::string& story_id, fuchsia::modular::Proposal proposal) {
  auto prototype = std::make_unique<SuggestionPrototype>(
      source_url, story_id, std::move(proposal));
  auto prototype_pair = owner->emplace(
      std::make_pair(source_url, prototype->proposal.id),
      std::move(prototype));
  return prototype_pair.first->second.get();
}

std::string SuggestionPrototype::ShortRepr() {
  std::ostringstream str;
  str << "proposal " << proposal.id << " from " << source_url;
  return str.str();
}

fuchsia::modular::Suggestion SuggestionPrototype::MakeSuggestion() const {
  fuchsia::modular::Suggestion suggestion;
  suggestion.uuid = suggestion_id;
  fidl::Clone(proposal.display, &suggestion.display);
  return suggestion;
}

}  // namespace modular
