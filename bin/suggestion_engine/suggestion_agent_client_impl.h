// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/suggestion_agent_client.fidl.h"
#include "apps/maxwell/src/suggestion_engine/agent_suggestion_record.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"
#include "apps/maxwell/src/bound_set.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace suggestion {

class Repo;

// SuggestionAgentClientImpl tracks proposals and their resulting suggestions
// from a single suggestion agent. Source entries are created on demand and kept
// alive as long as any proposals or publisher bindings exist.
class SuggestionAgentClientImpl : public SuggestionAgentClient {
 public:
  SuggestionAgentClientImpl(Repo* repo, const std::string& component_url)
      : repo_(repo), component_url_(component_url), bindings_(this) {}

  void AddBinding(fidl::InterfaceRequest<SuggestionAgentClient> request) {
    bindings_.emplace(
        new fidl::Binding<SuggestionAgentClient>(this, std::move(request)));
  }

  std::string component_url() const { return component_url_; }

  void Propose(ProposalPtr proposal) override;
  void Remove(const fidl::String& proposal_id) override;
  void GetAll(const GetAllCallback& callback) override;

  // TEMPORARY; TODO(rosswang): flatten record structures instead
  AgentSuggestionRecord* GetByProposalId(const std::string& proposal_id) {
    return &proposals_[proposal_id];
  }

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

  void OnNewProposal(ProposalPtr proposal, AgentSuggestionRecord* record);
  void OnChangeProposal(ProposalPtr proposal, AgentSuggestionRecord* record);

  bool ShouldEraseSelf() const {
    return proposals_.empty() && bindings_.empty();
  }
  void EraseSelf();

  Repo* const repo_;
  const std::string component_url_;
  // indexed by proposal ID
  std::unordered_map<std::string, AgentSuggestionRecord> proposals_;
  BindingSet bindings_;
};

}  // namespace suggestion
}  // namespace maxwell
