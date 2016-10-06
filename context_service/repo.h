// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/context_service/context_service.mojom.h"

#include "apps/maxwell/context_service/graph.h"

namespace intelligence {
namespace context_service {

// TODO(rosswang): V.5 query richness.
struct Query {
  Query(const std::string& label,
        const std::string& schema,
        ContextSubscriberLinkPtr subscriber)
      : label(label), schema(schema), subscriber(subscriber.Pass()) {}

  std::string label;
  std::string schema;
  ContextSubscriberLinkPtr subscriber;
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
  class QuerySet
      : public maxwell::BoundSet<struct Query, ContextSubscriberLink> {
   protected:
    ContextSubscriberLinkPtr* GetPtr(struct Query* element) override;
  };

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

  MOJO_DISALLOW_COPY_AND_ASSIGN(Repo);
};

}  // namespace context_service
}  // namespace intelligence
