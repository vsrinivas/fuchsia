// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/context/cpp/context_helper.h>

namespace modular {

std::optional<std::vector<fuchsia::modular::ContextValue>> TakeContextValue(
    fuchsia::modular::ContextUpdate* const update, const std::string& key) {
  for (auto& it : update->values) {
    if (it.key == key) {
      return std::move(it.value);
    }
  }
  return std::nullopt;
}

void AddToContextQuery(fuchsia::modular::ContextQuery* const query,
                       const std::string& key,
                       fuchsia::modular::ContextSelector selector) {
  fuchsia::modular::ContextQueryEntry entry;
  entry.key = key;
  entry.value = std::move(selector);
  query->selector.push_back(std::move(entry));
}

bool HasSelectorKey(fuchsia::modular::ContextQuery* const query,
                    const std::string& key) {
  for (auto& it : query->selector) {
    if (it.key == key) {
      return true;
    }
  }
  return false;
}

}  // namespace modular
