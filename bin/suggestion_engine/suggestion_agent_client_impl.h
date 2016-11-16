// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_agent_client.fidl.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_engine.h"

namespace maxwell {
namespace suggestion {

class SuggestionEngineApp;

// SuggestionAgentClientImpl tracks proposals and their resulting suggestions
// from a single suggestion agent. Source entries are created on demand and kept
// alive as long as any proposals or publisher bindings exist.
class SuggestionAgentClientImpl : public SuggestionAgentClient {
 public:
  SuggestionAgentClientImpl(SuggestionEngineApp* suggestinator,
                            const std::string& component_url)
      : suggestinator_(suggestinator),
        component_url_(component_url),
        bindings_(this) {}

  void AddBinding(fidl::InterfaceRequest<SuggestionAgentClient> request) {
    bindings_.emplace(
        new fidl::Binding<SuggestionAgentClient>(this, std::move(request)));
  }

  void Propose(ProposalPtr proposal) override;
  void Remove(const fidl::String& proposal_id) override;
  void GetAll(const GetAllCallback& callback) override;

 private:
  class BindingSet : public maxwell::BindingSet<SuggestionAgentClient> {
   public:
    BindingSet(SuggestionAgentClientImpl* impl) : impl_(impl) {}

   protected:
    void OnConnectionError(
        fidl::Binding<SuggestionAgentClient>* binding) override;

   private:
    SuggestionAgentClientImpl* const impl_;
  };

  void ProposalToSuggestion(const Proposal& proposal, Suggestion* suggestion) {
    // TODO(rosswang): real UUIDs
    suggestion->uuid =
        std::to_string(reinterpret_cast<size_t>(this)) + std::to_string(id_++);
    // TODO(rosswang): rank
    suggestion->rank = id_;  // shhh

    suggestion->display = proposal.display->Clone();
  }

  void BroadcastNewSuggestion(const Suggestion& suggestion);
  void BroadcastRemoveSuggestion(const Suggestion& suggestion);
  void OnNewProposal(const Proposal& proposal, Suggestion* suggestion);

  void OnChangeProposal(const Proposal& proposal, Suggestion* suggestion) {
    BroadcastRemoveSuggestion(*suggestion);

    // TODO(rosswang): re-rank if necessary
    suggestion->display = proposal.display->Clone();

    BroadcastNewSuggestion(*suggestion);
  }

  void EraseSelf();

  SuggestionEngineApp* const suggestinator_;
  const std::string component_url_;
  std::unordered_map<std::string, Suggestion> suggestions_;
  BindingSet bindings_;

  uint64_t id_;
};

}  // namespace suggestion
}  // namespace maxwell
