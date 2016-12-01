// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/repo.h"

namespace maxwell {

void Repo::Index(DataNode* data_node) {
  by_label_and_schema_[data_node->label].emplace(data_node->schema, data_node);

  // Wire up any matching queries (which could be seen as 3p indexing).
  for (auto query = queries_.begin(); query != queries_.end();) {
    if (query->label == data_node->label &&
        query->schema == data_node->schema) {
      // TODO(rosswang): notify matches for open-ended queries; switch
      // listener for singleton-result queries
      data_node->Subscribe(std::move(query->subscriber));
      query = queries_.erase(query);
    } else {
      ++query;
    }
  }
}

void Repo::Query(const std::string& label,
                 const std::string& schema,
                 ContextSubscriberLinkPtr subscriber) {
  auto repo_by_schema = by_label_and_schema_.find(label);
  if (repo_by_schema == by_label_and_schema_.end()) {
    queries_.emplace(label, schema, std::move(subscriber));
  } else {
    auto result = repo_by_schema->second.find(schema);
    if (result == repo_by_schema->second.end()) {
      queries_.emplace(label, schema, std::move(subscriber));
    } else {
      result->second->Subscribe(std::move(subscriber));
    }
  }
}

}  // namespace maxwell
