// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/repo.h"

namespace maxwell {

ProposalPublisherImpl* Repo::GetOrCreateSourceClient(
    const std::string& component_url) {
  std::unique_ptr<ProposalPublisherImpl>& source = sources_[component_url];
  if (!source)  // create if it didn't already exist
    source.reset(new ProposalPublisherImpl(this, component_url));

  return source.get();
}

SuggestionPrototype* Repo::AddSuggestion(ProposalPublisherImpl* source,
                                         ProposalPtr proposal) {
  // Assert source registered; this isn't strictly necessary but makes sense.
  FTL_CHECK(sources_[source->component_url()].get() == source);

  std::string id = RandomUuid();
  SuggestionPrototype* prototype = &suggestions_[id];
  prototype->suggestion_id = id;
  prototype->source = source;
  prototype->timestamp = ftl::TimePoint::Now();
  prototype->proposal = std::move(proposal);

  // TODO(rosswang): proper channel routing. For now, add to all channels
  auto next_entry = next_channel_.OnAddSuggestion(prototype);
  if (next_entry) {
    prototype->ranks_by_channel[&next_channel_] = next_entry;
  }
  for (auto& ask_channel : ask_channels_) {
    prototype->ranks_by_channel[ask_channel.get()] =
        ask_channel->OnAddSuggestion(prototype);
  }

  return prototype;
}

void Repo::InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                       fidl::InterfaceRequest<AskController> controller) {
  auto ask = std::make_unique<AskChannel>(this, std::move(listener),
                                          std::move(controller));

  // Bootstrap with existing next suggestions
  for (auto& ranked_suggestion : *next_channel_.ranked_suggestions()) {
    // const_cast is okay because Repo owns all prototypes mutably.
    auto suggestion_prototype =
        const_cast<SuggestionPrototype*>(ranked_suggestion->prototype);
    suggestion_prototype->ranks_by_channel[ask.get()] =
        ask->OnAddSuggestion(suggestion_prototype);
  }

  ask_channels_.emplace(std::move(ask));
}

}  // namespace maxwell
