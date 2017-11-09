// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_

#include <list>
#include <vector>

#include "lib/suggestion/fidl/debug.fidl.h"
#include "lib/suggestion/fidl/proposal.fidl.h"
#include "lib/suggestion/fidl/user_input.fidl.h"

#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

namespace maxwell {

// Provides a debug interface that is accessible through the MI dashboard.
class SuggestionDebugImpl : public SuggestionDebug {
 public:
  SuggestionDebugImpl();

  void OnAskStart(std::string query, const RankedSuggestionsList* suggestions);
  void OnSuggestionSelected(const SuggestionPrototype* selected_suggestion);
  void OnInterrupt(const SuggestionPrototype* interrupt_suggestion);
  void OnNextUpdate(const RankedSuggestionsList* suggestions);

 private:
  // |SuggestionDebug|
  void WatchAskProposals(
      fidl::InterfaceHandle<AskProposalListener> listener) override;

  fidl::InterfacePtrSet<AskProposalListener> ask_proposal_listeners_;

  // |SuggestionDebug|
  void WatchInterruptionProposals(
      fidl::InterfaceHandle<InterruptionProposalListener> listener) override;

  fidl::InterfacePtrSet<InterruptionProposalListener>
      interruption_proposal_listeners_;

  // |SuggestionDebug|
  void WatchNextProposals(
      fidl::InterfaceHandle<NextProposalListener> listener) override;

  fidl::InterfacePtrSet<NextProposalListener> next_proposal_listeners_;

  // The cached set of next proposals.
  fidl::Array<ProposalSummaryPtr> cached_next_proposals_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_DEBUG_H_
