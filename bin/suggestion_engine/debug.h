// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_

#include <list>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/lib/util/idle_waiter.h"

namespace modular {

// Provides a debug interface that is accessible through the MI dashboard.
class SuggestionDebugImpl : public fuchsia::modular::SuggestionDebug {
 public:
  SuggestionDebugImpl();
  ~SuggestionDebugImpl() override;

  fxl::WeakPtr<SuggestionDebugImpl> GetWeakPtr();

  void OnAskStart(std::string query, const RankedSuggestionsList* suggestions);
  void OnSuggestionSelected(const SuggestionPrototype* selected_suggestion);
  void OnInterrupt(const SuggestionPrototype* interrupt_suggestion);
  void OnNextUpdate(const RankedSuggestionsList* suggestions);

  util::IdleWaiter* GetIdleWaiter();

 private:
  // |fuchsia::modular::SuggestionDebug|
  void WatchAskProposals(
      fidl::InterfaceHandle<fuchsia::modular::AskProposalListener> listener)
      override;
  // |fuchsia::modular::SuggestionDebug|
  void WatchInterruptionProposals(
      fidl::InterfaceHandle<fuchsia::modular::InterruptionProposalListener>
          listener) override;
  // |fuchsia::modular::SuggestionDebug|
  void WatchNextProposals(
      fidl::InterfaceHandle<fuchsia::modular::NextProposalListener> listener)
      override;
  // |fuchsia::modular::SuggestionDebug|
  void WaitUntilIdle(WaitUntilIdleCallback callback) override;

  fidl::InterfacePtrSet<fuchsia::modular::AskProposalListener>
      ask_proposal_listeners_;
  fidl::InterfacePtrSet<fuchsia::modular::InterruptionProposalListener>
      interruption_proposal_listeners_;
  fidl::InterfacePtrSet<fuchsia::modular::NextProposalListener>
      next_proposal_listeners_;

  // The cached set of next proposals.
  fidl::VectorPtr<fuchsia::modular::ProposalSummary> cached_next_proposals_;

  util::IdleWaiter idle_waiter_;
  fxl::WeakPtrFactory<SuggestionDebugImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
