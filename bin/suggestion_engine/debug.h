// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_

#include <list>
#include <vector>

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/suggestion/fidl/debug.fidl.h"
#include "lib/suggestion/fidl/proposal.fidl.h"
#include "lib/suggestion/fidl/user_input.fidl.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/lib/util/wait_until_idle.h"

namespace maxwell {

// Provides a debug interface that is accessible through the MI dashboard.
class SuggestionDebugImpl : public SuggestionDebug {
 public:
  SuggestionDebugImpl();
  ~SuggestionDebugImpl() override;

  fxl::WeakPtr<SuggestionDebugImpl> GetWeakPtr();

  void OnAskStart(std::string query, const RankedSuggestionsList* suggestions);
  void OnSuggestionSelected(const SuggestionPrototype* selected_suggestion);
  void OnInterrupt(const SuggestionPrototype* interrupt_suggestion);
  void OnNextUpdate(const RankedSuggestionsList* suggestions);

  // Forwards to |IdleWaiter::RegisterOngoingActivity|
  util::IdleWaiter::ActivityToken RegisterOngoingActivity();
  // Forwards to |IdleWaiter::FinishIdleCheck|
  bool FinishIdleCheck();

 private:
  // |SuggestionDebug|
  void WatchAskProposals(
      f1dl::InterfaceHandle<AskProposalListener> listener) override;
  // |SuggestionDebug|
  void WatchInterruptionProposals(
      f1dl::InterfaceHandle<InterruptionProposalListener> listener) override;
  // |SuggestionDebug|
  void WatchNextProposals(
      f1dl::InterfaceHandle<NextProposalListener> listener) override;
  // |SuggestionDebug|
  void WaitUntilIdle(const WaitUntilIdleCallback& callback) override;

  f1dl::InterfacePtrSet<AskProposalListener> ask_proposal_listeners_;
  f1dl::InterfacePtrSet<InterruptionProposalListener>
      interruption_proposal_listeners_;
  f1dl::InterfacePtrSet<NextProposalListener> next_proposal_listeners_;

  // The cached set of next proposals.
  f1dl::VectorPtr<ProposalSummaryPtr> cached_next_proposals_;

  util::IdleWaiter wait_until_idle_;
  fxl::WeakPtrFactory<SuggestionDebugImpl> weak_ptr_factory_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
