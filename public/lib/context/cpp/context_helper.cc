// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/context/cpp/context_helper.h"

namespace modular {

std::pair<bool, fidl::VectorPtr<modular::ContextValue>> TakeContextValue(
    modular::ContextUpdate* const update, const std::string& key) {
  for (auto& it: update->values.take()) {
    if (it.key == key) {
      return std::make_pair(true, std::move(it.value));
    }
  }
  return std::make_pair(false, fidl::VectorPtr<modular::ContextValue>());
}

void AddToContextQuery(modular::ContextQuery* const query,
                       const std::string& key,
                       modular::ContextSelector selector) {
  modular::ContextQueryEntry entry;
  entry.key = key;
  entry.value = std::move(selector);
  query->selector.push_back(std::move(entry));
}

bool HasSelectorKey(modular::ContextQuery* const query,
                    const std::string& key) {
  for (auto& it : *query->selector) {
    if (it.key == key) {
      return true;
    }
  }
  return false;
}

}  // namespace modular
