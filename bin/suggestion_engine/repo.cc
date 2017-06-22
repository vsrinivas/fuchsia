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

void Repo::AddSuggestion(SuggestionPrototype* prototype,
                         SuggestionChannel* channel) {
  prototype->suggestion_id = RandomUuid();
  if (channel) {
    FTL_VLOG(2) << "Adding suggestion "
                << prototype->proposal->display->headline
                << " on a specific channel.";
    channel->OnAddSuggestion(prototype);
  } else {
    FTL_VLOG(2) << "Adding suggestion "
                << prototype->proposal->display->headline << " on next + "
                << ask_channels_.size() << " ask channels.";
    next_channel_.OnAddSuggestion(prototype);
    for (auto& ask_channel : ask_channels_) {
      ask_channel->OnAddSuggestion(prototype);
    }
  }
  suggestions_[prototype->suggestion_id] = prototype;
}

void Repo::InitiateAsk(fidl::InterfaceHandle<SuggestionListener> listener,
                       fidl::InterfaceRequest<AskController> controller) {
  auto ask = std::make_unique<AskChannel>(this, std::move(listener),
                                          std::move(controller), debug_);
  // Bootstrap with existing next suggestions
  for (auto& ranked_suggestion : *next_channel_.ranked_suggestions()) {
    ask->OnAddSuggestion(ranked_suggestion->prototype);
  }

  ask_channels_.emplace(std::move(ask));
}

std::unique_ptr<SuggestionPrototype> Repo::Extract(const std::string& id) {
  auto it = suggestions_.find(id);
  if (it == suggestions_.end()) {
    return NULL;
  } else {
    const SuggestionPrototype* prototype = suggestions_[id];
    return prototype->source->Extract(prototype->proposal->id);
  }
}

}  // namespace maxwell
