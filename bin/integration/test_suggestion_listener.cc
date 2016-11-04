// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/integration/test_suggestion_listener.h"

#include "apps/maxwell/lib/suggestion/formatting.h"

void TestSuggestionListener::OnAdd(
    fidl::Array<maxwell::suggestion::SuggestionPtr> suggestions) {
  FTL_LOG(INFO) << "OnAdd(" << suggestions << ")";
  naive_suggestion_count_ += suggestions.size();
  for (auto& suggestion : suggestions)
    suggestions_[suggestion->uuid] = std::move(suggestion);

  EXPECT_EQ(naive_suggestion_count_, (signed)suggestions_.size());
}

void TestSuggestionListener::OnRemove(const fidl::String& uuid) {
  FTL_LOG(INFO) << "OnRemove(" << uuid << ")";
  naive_suggestion_count_--;
  suggestions_.erase(uuid);

  EXPECT_EQ(naive_suggestion_count_, (signed)suggestions_.size());
}

void TestSuggestionListener::OnRemoveAll() {
  FTL_LOG(INFO) << "OnRemoveAll";
  naive_suggestion_count_ = 0;
  suggestions_.clear();
}
