// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test_suggestion_listener.h"

#include <lib/suggestion/cpp/formatting.h>

bool suggestion_less(const fuchsia::modular::Suggestion* a,
                     const fuchsia::modular::Suggestion* b) {
  return a->confidence > b->confidence;
}

namespace modular {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::VectorPtr<T>& value) {
  os << "[ ";
  for (const T& element : *value) {
    os << element << " ";
  }
  return os << "]";
}

void TestSuggestionListener::OnInterrupt(
    fuchsia::modular::Suggestion suggestion) {
  FXL_LOG(INFO) << "OnInterrupt(" << suggestion.uuid << ")";

  ClearSuggestions();

  auto insert_head = ordered_suggestions_.begin();
  insert_head = std::upper_bound(insert_head, ordered_suggestions_.end(),
                                 &suggestion, suggestion_less);
  suggestions_by_id_[suggestion.uuid] = fuchsia::modular::Suggestion();
  fidl::Clone(suggestion, &suggestions_by_id_[suggestion.uuid]);
  insert_head = ordered_suggestions_.insert(
                    insert_head, &suggestions_by_id_[suggestion.uuid]) +
                1;

  EXPECT_EQ((signed)ordered_suggestions_.size(),
            (signed)suggestions_by_id_.size());
}

void TestSuggestionListener::OnNextResults(
    fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) {
  FXL_LOG(INFO) << "OnNextResults(" << suggestions << ")";

  OnAnyResults(suggestions);
}

void TestSuggestionListener::OnQueryResults(
    fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) {
  FXL_LOG(INFO) << "OnQueryResults(" << suggestions << ")";

  OnAnyResults(suggestions);
}

void TestSuggestionListener::OnAnyResults(
    fidl::VectorPtr<fuchsia::modular::Suggestion>& suggestions) {
  ClearSuggestions();

  auto insert_head = ordered_suggestions_.begin();
  for (auto& suggestion : *suggestions) {
    insert_head = std::upper_bound(insert_head, ordered_suggestions_.end(),
                                   &suggestion, suggestion_less);
    suggestions_by_id_[suggestion.uuid] = fuchsia::modular::Suggestion();
    fidl::Clone(suggestion, &suggestions_by_id_[suggestion.uuid]);
    insert_head = ordered_suggestions_.insert(
                      insert_head, &suggestions_by_id_[suggestion.uuid]) +
                  1;
  }

  EXPECT_EQ((signed)ordered_suggestions_.size(),
            (signed)suggestions_by_id_.size());
}

void TestSuggestionListener::ClearSuggestions() {
  // For use when the listener_binding_ is reset
  ordered_suggestions_.clear();
  suggestions_by_id_.clear();
  query_complete_ = false;
}

void TestSuggestionListener::OnProcessingChange(bool processing) {
  FXL_LOG(INFO) << "OnProcessingChange to " << processing;
}

void TestSuggestionListener::OnQueryComplete() {
  FXL_LOG(INFO) << "OnQueryComplete";
  query_complete_ = true;
}

}  // namespace modular
