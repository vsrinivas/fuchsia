// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "gtest/gtest.h"

class TestSuggestionListener : public maxwell::Listener {
 public:
  void OnAdd(fidl::Array<maxwell::SuggestionPtr> suggestions) override;
  void OnRemove(const fidl::String& uuid) override;
  void OnRemoveAll() override;

  int suggestion_count() const { return naive_suggestion_count_; }

  // Exposes a pointer to the only suggestion in this listener. Retains
  // ownership of the pointer.
  const maxwell::Suggestion* GetOnlySuggestion() const {
    EXPECT_EQ(1, suggestion_count());
    return ordered_suggestions_.front();
  }

  const maxwell::Suggestion* operator[](int index) const {
    return ordered_suggestions_[index];
  }

  const maxwell::Suggestion* operator[](const std::string& id) const {
    auto it = suggestions_by_id_.find(id);
    return it == suggestions_by_id_.end() ? NULL : it->second.get();
  }

 private:
  int naive_suggestion_count_ = 0;
  std::unordered_map<std::string, maxwell::SuggestionPtr> suggestions_by_id_;
  std::vector<maxwell::Suggestion*> ordered_suggestions_;
};
