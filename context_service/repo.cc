// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "repo.h"

namespace intelligence {
namespace context_service {

using std::string;

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
    AddPendingQuery(label, schema, subscriber.Pass());
  } else {
    auto result = repo_by_schema->second.find(schema);
    if (result == repo_by_schema->second.end()) {
      AddPendingQuery(label, schema, subscriber.Pass());
    } else {
      result->second->Subscribe(subscriber.Pass());
    }
  }
}

void Repo::AddPendingQuery(const string& label,
                           const string& schema,
                           ContextSubscriberLinkPtr subscriber) {
  // TODO(rosswang): Once we support open-ended queries, we'll want to
  // watch queries even if they do have matches already.

  // Taken from mojo::InterfacePtrSet; remove link on error.
  ContextSubscriberLink* ifc = subscriber.get();
  subscriber.set_connection_error_handler([this, ifc] {
    auto it = std::find_if(queries_.begin(), queries_.end(),
                           [ifc](const struct Query& query) {
                             return query.subscriber.get() == ifc;
                           });

    assert(it != queries_.end());
    queries_.erase(it);
  });

  queries_.emplace_back(label, schema, subscriber.Pass());
}

}  // namespace context_service
}  // namespace intelligence
