// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_
#define PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_

#include <map>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace modular {

class TestSuggestionListener : public fuchsia::modular::NextListener,
                               public fuchsia::modular::QueryListener,
                               public fuchsia::modular::InterruptionListener {
 public:
  // |fuchsia::modular::InterruptionListener|
  void OnInterrupt(fuchsia::modular::Suggestion suggestion) override;

  // |fuchsia::modular::NextListener|
  void OnNextResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override;

  // |fuchsia::modular::NextListener|
  void OnProcessingChange(bool processing) override;

  // |fuchsia::modular::QueryListener|
  void OnQueryResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override;

  // |fuchsia::modular::QueryListener|
  void OnQueryComplete() override;

  int suggestion_count() const { return (signed)ordered_suggestions_.size(); }

  void ClearSuggestions();

  // Exposes a pointer to the only suggestion in this listener. Retains
  // ownership of the pointer.
  const fuchsia::modular::Suggestion* GetOnlySuggestion() const {
    EXPECT_EQ(1, suggestion_count());
    return GetTopSuggestion();
  }

  // Exposes a pointer to the top suggestion in this listener. Retains
  // ownership of the pointer.
  const fuchsia::modular::Suggestion* GetTopSuggestion() const {
    EXPECT_GE(suggestion_count(), 1);
    return ordered_suggestions_.front();
  }

  const fuchsia::modular::Suggestion* operator[](int index) const {
    return ordered_suggestions_[index];
  }

  const fuchsia::modular::Suggestion* operator[](const std::string& id) const {
    auto it = suggestions_by_id_.find(id);
    return it == suggestions_by_id_.end() ? NULL : &it->second;
  }

  const std::vector<fuchsia::modular::Suggestion*>& GetSuggestions() {
    return ordered_suggestions_;
  }

 private:
  void OnAnyResults(fidl::VectorPtr<fuchsia::modular::Suggestion>& suggestions);

  std::map<std::string, fuchsia::modular::Suggestion> suggestions_by_id_;
  std::vector<fuchsia::modular::Suggestion*> ordered_suggestions_;
};

class TestProposalListener {
 public:
  const std::vector<fuchsia::modular::ProposalSummary>& GetProposals() {
    return proposals_;
  }
  int proposal_count() const { return proposals_.size(); }

 protected:
  void UpdateProposals(
      fidl::VectorPtr<fuchsia::modular::ProposalSummary> proposals) {
    proposals_.clear();
    for (size_t i = 0; i < proposals->size(); ++i) {
      proposals_.push_back(std::move(proposals->at(i)));
    }
  }
  std::vector<fuchsia::modular::ProposalSummary> proposals_;
};

class TestDebugNextListener : public fuchsia::modular::NextProposalListener,
                              public TestProposalListener {
 public:
  void OnNextUpdate(
      fidl::VectorPtr<fuchsia::modular::ProposalSummary> proposals) override {
    FXL_LOG(INFO) << "In OnNextUpdate debug";
    UpdateProposals(std::move(proposals));
  }
};

class TestDebugAskListener : public fuchsia::modular::AskProposalListener,
                             public TestProposalListener {
 public:
  void OnAskStart(
      fidl::StringPtr query,
      fidl::VectorPtr<fuchsia::modular::ProposalSummary> proposals) override {
    UpdateProposals(std::move(proposals));
    query_ = query.get();
  }
  void OnProposalSelected(
      fuchsia::modular::ProposalSummaryPtr selectedProposal) override {
    selected_proposal_ = selectedProposal.get();
  }
  const std::string get_query() { return query_; }
  fuchsia::modular::ProposalSummary* get_selected_proposal() {
    return selected_proposal_;
  }

 private:
  std::string query_;
  fuchsia::modular::ProposalSummary* selected_proposal_;
};

class TestDebugInterruptionListener
    : public fuchsia::modular::InterruptionProposalListener {
 public:
  void OnInterrupt(
      fuchsia::modular::ProposalSummary interruptionProposal) override {
    interrupt_proposal_ = std::move(interruptionProposal);
  }
  fuchsia::modular::ProposalSummary get_interrupt_proposal() {
    fuchsia::modular::ProposalSummary result;
    fidl::Clone(interrupt_proposal_, &result);
    return result;
  }

 private:
  fuchsia::modular::ProposalSummary interrupt_proposal_;
};

}  // namespace modular

#endif  // PERIDOT_TESTS_MAXWELL_INTEGRATION_TEST_SUGGESTION_LISTENER_H_
