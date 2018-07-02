// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_PROPOSAL_PUBLISHER_IMPL_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_PROPOSAL_PUBLISHER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/bound_set/bound_set.h"

namespace modular {

class SuggestionEngineImpl;

/*
  ProposalPublisherImpl tracks proposals and their resulting suggestions
  from a single suggestion agent. Source entries are created on demand and kept
  alive as long as any proposals or publisher bindings exist.

 TODO: The component_url should eventually be replaced with a more consistent
  identifier that's reused across components to identify specific executables.
*/
class ProposalPublisherImpl : public fuchsia::modular::ProposalPublisher {
 public:
  ProposalPublisherImpl(SuggestionEngineImpl* engine,
                        const std::string& component_url);

  ~ProposalPublisherImpl() override;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher> request);

  void Propose(fuchsia::modular::Proposal proposal) override;
  void Remove(fidl::StringPtr proposal_id) override;

  void ProposeNavigation(
      fuchsia::modular::NavigationAction navigation) override;

  const std::string component_url() { return component_url_; }

 private:
  class BindingSet
      : public ::modular::BindingSet<fuchsia::modular::ProposalPublisher> {
   public:
    BindingSet(ProposalPublisherImpl* impl);
    ~BindingSet() override;

   protected:
    void OnConnectionError(
        fidl::Binding<fuchsia::modular::ProposalPublisher>* binding) override;

   private:
    ProposalPublisherImpl* const impl_;
  };

  bool ShouldEraseSelf() const;
  void EraseSelf();

  SuggestionEngineImpl* const engine_;
  const std::string component_url_;
  BindingSet bindings_;

  fxl::WeakPtrFactory<ProposalPublisherImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_PROPOSAL_PUBLISHER_IMPL_H_
