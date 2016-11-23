// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/repo.h"

namespace maxwell {
namespace suggestion {

SuggestionAgentClientImpl* Repo::GetOrCreateSourceClient(
    const std::string& component_url) {
  std::unique_ptr<SuggestionAgentClientImpl>& source = sources_[component_url];
  if (!source)  // create if it didn't already exist
    source.reset(new SuggestionAgentClientImpl(this, component_url));

  return source.get();
}

void Repo::AddSuggestion(std::unique_ptr<ProposalRecord> proposal,
                         AgentSuggestionRecord* agent_suggestion_record) {
  // Assert source registered; this isn't strictly necessary but makes sense.
  assert(sources_[proposal->source->component_url()].get() == proposal->source);

  agent_suggestion_record->suggestion_prototype =
      &*suggestions_.emplace(RandomUuid(), std::move(proposal)).first;

  // TODO(rosswang): proper channel routing. For now, add to all channels
  agent_suggestion_record->ranks_by_channel[&next_channel_] =
      next_channel_.OnAddSuggestion(
          agent_suggestion_record->suggestion_prototype);
  for (auto& ask_channel : ask_channels_) {
    agent_suggestion_record->ranks_by_channel[ask_channel.get()] =
        ask_channel->OnAddSuggestion(
            agent_suggestion_record->suggestion_prototype);
  }
}

}  // namespace suggestion
}  // namespace maxwell
