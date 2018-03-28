// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_
#define PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_

#include <map>

#include <fuchsia/cpp/modular.h>

#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace modular {

class TestSuggestionListener : public modular::NextListener,
                               public modular::QueryListener,
                               public modular::InterruptionListener {
 public:
  // |InterruptionListener|
  void OnInterrupt(modular::Suggestion suggestion) override;

  // |NextListener|
  void OnNextResults(fidl::VectorPtr<modular::Suggestion> suggestions) override;

  // |NextListener|
  void OnProcessingChange(bool processing) override;

  // |QueryListener|
  void OnQueryResults(fidl::VectorPtr<modular::Suggestion> suggestions) override;

  // |QueryListener|
  void OnQueryComplete() override;

  int suggestion_count() const { return (signed)ordered_suggestions_.size(); }

  void ClearSuggestions();

  // Exposes a pointer to the only suggestion in this listener. Retains
  // ownership of the pointer.
  const modular::Suggestion* GetOnlySuggestion() const {
    EXPECT_EQ(1, suggestion_count());
    return GetTopSuggestion();
  }

  // Exposes a pointer to the top suggestion in this listener. Retains
  // ownership of the pointer.
  const modular::Suggestion* GetTopSuggestion() const {
    EXPECT_GE(suggestion_count(), 1);
    return ordered_suggestions_.front();
  }

  const modular::Suggestion* operator[](int index) const {
    return ordered_suggestions_[index];
  }

  const modular::Suggestion* operator[](const std::string& id) const {
    auto it = suggestions_by_id_.find(id);
    return it == suggestions_by_id_.end() ? NULL : &it->second;
  }

  const std::vector<modular::Suggestion*>& GetSuggestions() {
    return ordered_suggestions_;
  }

 private:
  void OnAnyResults(fidl::VectorPtr<modular::Suggestion>& suggestions);

  std::map<std::string, modular::Suggestion> suggestions_by_id_;
  std::vector<modular::Suggestion*> ordered_suggestions_;
};

class TestProposalListener {
 public:
  const std::vector<modular::ProposalSummary>& GetProposals() {
    return proposals_;
  }
  int proposal_count() const { return proposals_.size(); }

 protected:
  void UpdateProposals(fidl::VectorPtr<modular::ProposalSummary> proposals) {
    proposals_.clear();
    for (size_t i = 0; i < proposals->size(); ++i) {
      proposals_.push_back(std::move(proposals->at(i)));
    }
  }
  std::vector<modular::ProposalSummary> proposals_;
};

class TestDebugNextListener : public modular::NextProposalListener,
                              public TestProposalListener {
 public:
  void OnNextUpdate(
      fidl::VectorPtr<modular::ProposalSummary> proposals) override {
    FXL_LOG(INFO) << "In OnNextUpdate debug";
    UpdateProposals(std::move(proposals));
  }
};

class TestDebugAskListener : public modular::AskProposalListener,
                             public TestProposalListener {
 public:
  void OnAskStart(fidl::StringPtr query,
                  fidl::VectorPtr<modular::ProposalSummary> proposals) override {
    UpdateProposals(std::move(proposals));
    query_ = query.get();
  }
  void OnProposalSelected(
      modular::ProposalSummaryPtr selectedProposal) override {
    selected_proposal_ = selectedProposal.get();
  }
  const std::string get_query() { return query_; }
  modular::ProposalSummary* get_selected_proposal() {
    return selected_proposal_;
  }

 private:
  std::string query_;
  modular::ProposalSummary* selected_proposal_;
};

class TestDebugInterruptionListener
    : public modular::InterruptionProposalListener {
 public:
  void OnInterrupt(modular::ProposalSummary interruptionProposal) override {
    interrupt_proposal_ = std::move(interruptionProposal);
  }
  modular::ProposalSummary get_interrupt_proposal() {
    modular::ProposalSummary result;
    fidl::Clone(interrupt_proposal_, &result);
    return result;
  }

 private:
  modular::ProposalSummary interrupt_proposal_;
};

}  // namespace modular

#endif  // PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_
