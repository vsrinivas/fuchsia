// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/repo.h"

namespace maxwell {

void Repo::Index(DataNode* data_node) {
  const std::string& label = data_node->label;
  by_label_[label] = data_node;

  // Wire up any pending queries.
  auto it = pending_queries_.find(label);
  if (it == pending_queries_.end())
    return;

  auto& subscribers = it->second;
  for (auto& subscriber_link : subscribers) {
    data_node->Subscribe(std::move(subscriber_link));
  }

  pending_queries_.erase(it);
}

void Repo::Query(const std::string& label,
                 ContextSubscriberLinkPtr subscriber) {
  auto it = by_label_.find(label);
  if (it == by_label_.end()) {
    pending_queries_[label].emplace_back(std::move(subscriber));
  } else {
    it->second->Subscribe(std::move(subscriber));
  }
}

}  // namespace maxwell
