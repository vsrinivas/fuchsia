// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/suggestion/debug.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "gtest/gtest.h"

class TestSuggestionListener : public maxwell::SuggestionListener {
 public:
  void OnAdd(fidl::Array<maxwell::SuggestionPtr> suggestions) override;
  void OnRemove(const fidl::String& uuid) override;
  void OnRemoveAll() override;

  int suggestion_count() const { return (signed)ordered_suggestions_.size(); }

  // Exposes a pointer to the only suggestion in this listener. Retains
  // ownership of the pointer.
  const maxwell::Suggestion* GetOnlySuggestion() const {
    EXPECT_EQ(1, suggestion_count());
    return GetTopSuggestion();
  }

  // Exposes a pointer to the top suggestion in this listener. Retains
  // ownership of the pointer.
  const maxwell::Suggestion* GetTopSuggestion() const {
    EXPECT_GE(suggestion_count(), 1);
    return ordered_suggestions_.front();
  }

  const maxwell::Suggestion* operator[](int index) const {
    return ordered_suggestions_[index];
  }

  const maxwell::Suggestion* operator[](const std::string& id) const {
    auto it = suggestions_by_id_.find(id);
    return it == suggestions_by_id_.end() ? NULL : it->second.get();
  }

  const std::vector<maxwell::Suggestion*>& GetSuggestions() {
    return ordered_suggestions_;
  }

 private:
  std::unordered_map<std::string, maxwell::SuggestionPtr> suggestions_by_id_;
  std::vector<maxwell::Suggestion*> ordered_suggestions_;
};

class TestProposalListener {
 public:
  const std::vector<maxwell::ProposalSummaryPtr>& GetProposals() {
    return proposals_;
  }
  int proposal_count() const { return proposals_.size(); }

 protected:
  void UpdateProposals(fidl::Array<maxwell::ProposalSummaryPtr> proposals) {
    proposals_.clear();
    for (auto& proposal : proposals) {
      proposals_.push_back(std::move(proposal));
    }
  }
  std::vector<maxwell::ProposalSummaryPtr> proposals_;
};

class TestDebugNextListener : public maxwell::NextProposalListener,
                              public TestProposalListener {
 public:
  void OnNextUpdate(
      fidl::Array<maxwell::ProposalSummaryPtr> proposals) override {
    FXL_LOG(INFO) << "In OnNextUpdate debug";
    UpdateProposals(std::move(proposals));
  }
};

class TestDebugAskListener : public maxwell::AskProposalListener,
                             public TestProposalListener {
 public:
  void OnAskStart(const fidl::String& query,
                  fidl::Array<maxwell::ProposalSummaryPtr> proposals) override {
    UpdateProposals(std::move(proposals));
    query_ = query.get();
  }
  void OnProposalSelected(
      maxwell::ProposalSummaryPtr selectedProposal) override {
    selected_proposal_ = selectedProposal.get();
  }
  const std::string get_query() { return query_; }
  maxwell::ProposalSummary* get_selected_proposal() {
    return selected_proposal_;
  }

 private:
  std::string query_;
  maxwell::ProposalSummary* selected_proposal_;
};

class TestDebugInterruptionListener
    : public maxwell::InterruptionProposalListener {
 public:
  void OnInterrupt(maxwell::ProposalSummaryPtr interruptionProposal) override {
    interrupt_proposal_ = std::move(interruptionProposal);
  }
  maxwell::ProposalSummaryPtr get_interrupt_proposal() {
    return interrupt_proposal_.Clone();
  }

 private:
  maxwell::ProposalSummaryPtr interrupt_proposal_;
};
