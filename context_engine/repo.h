// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/context_engine.fidl.h"

#include "apps/maxwell/context_engine/graph.h"

namespace maxwell {
namespace context_engine {

// TODO(rosswang): V.5 query richness.
struct SimpleQuery {
  SimpleQuery(const std::string& label,
              const std::string& schema,
              ContextSubscriberLinkPtr subscriber)
      : label(label), schema(schema), subscriber(std::move(subscriber)) {}

  std::string label;
  std::string schema;
  ContextSubscriberLinkPtr subscriber;

  static ContextSubscriberLinkPtr* GetPtr(SimpleQuery* element) {
    return &element->subscriber;
  }
};

class DataNodeQueryIterator {
 public:
  typedef std::unordered_multimap<std::string, DataNode*>::const_iterator
      iterator;

  DataNodeQueryIterator(iterator it) : it_(it) {}

  DataNode& operator*() const { return *it_->second; }

  DataNode* operator->() const { return it_->second; }

  DataNodeQueryIterator& operator++() {
    ++it_;
    return *this;
  }

  bool operator==(DataNodeQueryIterator other) { return it_ == other.it_; }

 private:
  iterator it_;
};

class Repo {
 public:
  Repo() {}

  void Index(DataNode* data_node);
  void Query(const std::string& label,
             const std::string& schema,
             ContextSubscriberLinkPtr subscriber);

 private:
  typedef BoundPtrSet<ContextSubscriberLink, SimpleQuery, SimpleQuery::GetPtr>
      QuerySet;

  // TODO(rosswang): Is there a good way to not require a separate index
  // structure for each combination we can come up with?
  std::unordered_map<std::string,
                     std::unordered_multimap<std::string, DataNode*>>
      by_label_and_schema_;
  // std::unordered_multimap<std::string, DataNode*> by_schema_;

  // TODO(rosswang): Right now, this could just be a 2-D map. In general though,
  // since queries can have arbitrary dimensionality and complexity, I don't
  // know how well we can optimize this.
  QuerySet queries_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Repo);
};

}  // namespace context_engine
}  // namespace maxwell
