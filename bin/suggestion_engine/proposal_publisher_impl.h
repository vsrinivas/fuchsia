// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/proposal_publisher.fidl.h"
#include "apps/maxwell/src/bound_set.h"
#include "apps/maxwell/src/suggestion_engine/ranked_suggestion.h"
#include "apps/maxwell/src/suggestion_engine/repo.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace maxwell {

class Repo;

// ProposalPublisherImpl tracks proposals and their resulting suggestions
// from a single suggestion agent. Source entries are created on demand and kept
// alive as long as any proposals or publisher bindings exist.
class ProposalPublisherImpl : public ProposalPublisher {
 public:
  ProposalPublisherImpl(Repo* repo, const std::string& component_url)
      : repo_(repo),
        component_url_(component_url),
        bindings_(this),
        weak_ptr_factory_(this) {}

  void AddBinding(fidl::InterfaceRequest<ProposalPublisher> request) {
    bindings_.emplace(
        new fidl::Binding<ProposalPublisher>(this, std::move(request)));
  }

  std::string component_url() const { return component_url_; }

  void Propose(ProposalPtr proposal) override;
  void Propose(ProposalPtr proposal, SuggestionChannel* channel);
  void Remove(const fidl::String& proposal_id) override;
  void Remove(const std::string& proposal_id, SuggestionChannel* channel);
  void GetAll(const GetAllCallback& callback) override;
  void RegisterAskHandler(
      fidl::InterfaceHandle<AskHandler> ask_handler) override;
  // Returns the extracted suggestion prototype only if it was actually removed
  // from all channels, or nullptr otherwise. TODO(rosswang): this kind of
  // overloading is a hack; take care of it after we settle on the new service
  // API.
  std::unique_ptr<SuggestionPrototype> Extract(
      const std::string& id,
      SuggestionChannel* channel = nullptr);

 private:
  class BindingSet : public maxwell::BindingSet<ProposalPublisher> {
   public:
    BindingSet(ProposalPublisherImpl* impl) : impl_(impl) {}

   protected:
    void OnConnectionError(fidl::Binding<ProposalPublisher>* binding) override;

   private:
    ProposalPublisherImpl* const impl_;
  };

  void OnChangeProposal(ProposalPtr proposal,
                        SuggestionPrototype* suggestion_prototype);

  bool ShouldEraseSelf() const {
    return proposals_.empty() && bindings_.empty() &&
           !weak_ptr_factory_.HasWeakPtrs();
  }
  void EraseSelf();

  Repo* const repo_;
  const std::string component_url_;
  // indexed by proposal ID
  std::unordered_map<std::string, std::unique_ptr<SuggestionPrototype>>
      proposals_;
  BindingSet bindings_;

  ftl::WeakPtrFactory<ProposalPublisherImpl> weak_ptr_factory_;
};

}  // namespace maxwell
