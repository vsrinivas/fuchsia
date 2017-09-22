// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/suggestion/fidl/proposal_publisher.fidl.h"

#include "peridot/lib/bound_set/bound_set.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace maxwell {

class SuggestionEngineImpl;

/*
  ProposalPublisherImpl tracks proposals and their resulting suggestions
  from a single suggestion agent. Source entries are created on demand and kept
  alive as long as any proposals or publisher bindings exist.

 TODO: The component_url should eventually be replaced with a more consistent
  identifier that's reused across components to identify specific executables.
*/
class ProposalPublisherImpl : public ProposalPublisher {
 public:
  ProposalPublisherImpl(SuggestionEngineImpl* engine,
                        const std::string& component_url)
      : engine_(engine),
        component_url_(component_url),
        bindings_(this),
        weak_ptr_factory_(this) {}

  void AddBinding(fidl::InterfaceRequest<ProposalPublisher> request) {
    bindings_.emplace(
        new fidl::Binding<ProposalPublisher>(this, std::move(request)));
  }

  void Propose(ProposalPtr proposal) override;
  void Remove(const fidl::String& proposal_id) override;
  void GetAll(const GetAllCallback& callback) override;
  void RegisterAskHandler(
      fidl::InterfaceHandle<AskHandler> ask_handler) override;

  const std::string component_url() { return component_url_; }

 private:
  class BindingSet : public maxwell::BindingSet<ProposalPublisher> {
   public:
    BindingSet(ProposalPublisherImpl* impl) : impl_(impl) {}

   protected:
    void OnConnectionError(fidl::Binding<ProposalPublisher>* binding) override;

   private:
    ProposalPublisherImpl* const impl_;
  };

  bool ShouldEraseSelf() const {
    return bindings_.empty() && !weak_ptr_factory_.HasWeakPtrs();
  }
  void EraseSelf();

  SuggestionEngineImpl* const engine_;
  const std::string component_url_;
  BindingSet bindings_;

  fxl::WeakPtrFactory<ProposalPublisherImpl> weak_ptr_factory_;
};

}  // namespace maxwell
