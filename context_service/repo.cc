// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/context_service/repo.h"

namespace intelligence {
namespace context_service {

using std::string;

ContextSubscriberLinkPtr* Repo::QuerySet::GetPtr(struct Query* element) {
  return &element->subscriber;
}

void Repo::Index(DataNode* data_node) {
  by_label_and_schema_[data_node->label].emplace(data_node->schema, data_node);

  // Wire up any matching queries (which could be seen as 3p indexing).
  for (auto query = queries_.begin(); query != queries_.end();) {
    if (query->label == data_node->label &&
        query->schema == data_node->schema) {
      // TODO(rosswang): notify matches for open-ended queries; switch
      // listener for singleton-result queries
      data_node->Subscribe(query->subscriber.Pass());
      query = queries_.erase(query);
    } else {
      ++query;
    }
  }
}

void Repo::Query(const string& label,
                 const string& schema,
                 ContextSubscriberLinkPtr subscriber) {
  auto repo_by_schema = by_label_and_schema_.find(label);
  if (repo_by_schema == by_label_and_schema_.end()) {
    queries_.emplace(label, schema, subscriber.Pass());
  } else {
    auto result = repo_by_schema->second.find(schema);
    if (result == repo_by_schema->second.end()) {
      queries_.emplace(label, schema, subscriber.Pass());
    } else {
      result->second->Subscribe(subscriber.Pass());
    }
  }
}

}  // namespace context_service
}  // namespace intelligence
