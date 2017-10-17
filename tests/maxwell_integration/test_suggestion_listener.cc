// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/maxwell_integration/test_suggestion_listener.h"

#include "lib/suggestion/cpp/formatting.h"

bool suggestion_less(const maxwell::Suggestion* a,
                     const maxwell::Suggestion* b) {
  return a->confidence > b->confidence;
}

void TestSuggestionListener::OnAdd(
    fidl::Array<maxwell::SuggestionPtr> suggestions) {
  FXL_LOG(INFO) << "OnAdd(" << suggestions << ")";

  // Since OnAdd receives a snapshot of changes with self-consistent ordering
  // (TODO(rosswang): behavior not documented), we don't have to re-search from
  // the beginning every time (though this is not a significant savings).
  auto insert_head = ordered_suggestions_.begin();
  for (auto& suggestion : suggestions) {
    insert_head = std::upper_bound(insert_head, ordered_suggestions_.end(),
                                   suggestion.get(), suggestion_less);
    insert_head =
        ordered_suggestions_.emplace(insert_head, suggestion.get()) + 1;
    suggestions_by_id_[suggestion->uuid] = std::move(suggestion);
  }

  EXPECT_EQ((signed)ordered_suggestions_.size(),
            (signed)suggestions_by_id_.size());
}

void TestSuggestionListener::OnRemove(const fidl::String& uuid) {
  FXL_LOG(INFO) << "OnRemove(" << uuid << ")";
  auto it = suggestions_by_id_.find(uuid);
  auto range =
      std::equal_range(ordered_suggestions_.begin(), ordered_suggestions_.end(),
                       it->second.get(), suggestion_less);
  ordered_suggestions_.erase(
      std::remove(range.first, range.second, it->second.get()), range.second);
  suggestions_by_id_.erase(it);

  EXPECT_EQ((signed)ordered_suggestions_.size(),
            (signed)suggestions_by_id_.size());
}

void TestSuggestionListener::OnRemoveAll() {
  FXL_LOG(INFO) << "OnRemoveAll";
  ordered_suggestions_.clear();
  suggestions_by_id_.clear();
}

void TestSuggestionListener::OnProcessingChange(bool processing) {
  FXL_LOG(INFO) << "OnProcessingChange to " << processing;
}
