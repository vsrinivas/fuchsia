// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"

namespace modular {

std::string RandomUuid() {
  static uint64_t id = 0;
  // TODO(rosswang): real UUIDs
  return std::to_string(id++);
}

SuggestionPrototype* CreateSuggestionPrototype(
    SuggestionPrototypeMap* owner,
    const std::string& source_url,
    Proposal proposal) {
  auto prototype_pair = owner->emplace(std::make_pair(source_url, proposal.id),
                                       std::make_unique<SuggestionPrototype>());
  auto suggestion_prototype = prototype_pair.first->second.get();
  suggestion_prototype->suggestion_id = RandomUuid();
  suggestion_prototype->source_url = source_url;
  suggestion_prototype->timestamp = fxl::TimePoint::Now();
  suggestion_prototype->proposal = std::move(proposal);

  return suggestion_prototype;
}

}  // namespace modular
