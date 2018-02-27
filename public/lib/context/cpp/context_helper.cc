// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_helper.h"

namespace maxwell {

std::pair<bool, f1dl::Array<ContextValuePtr>> TakeContextValue(
    ContextUpdate* const update, const std::string& key) {
  for (auto& it: update->values) {
    if (it->key == key) {
      return std::make_pair(true, std::move(it->value));
    }
  }
  return std::make_pair(false, f1dl::Array<ContextValuePtr>());
}

void AddToContextQuery(ContextQuery* const query, const std::string& key,
                       ContextSelectorPtr selector) {
  auto entry = ContextQueryEntry::New();
  entry->key = key;
  entry->value = std::move(selector);
  query->selector.push_back(std::move(entry));
}

}  // namespace maxwell
