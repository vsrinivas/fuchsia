// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/services/context/subscriber_link.fidl.h"
#include "apps/maxwell/src/context_engine/graph.h"

namespace maxwell {

class Repo {
 public:
  Repo() {}

  void Index(DataNode* data_node);
  void Query(const std::string& label, ContextSubscriberLinkPtr subscriber);

 private:
  std::unordered_map<std::string, DataNode*> by_label_;
  // These are queries for which no data exists yet. We save them here until
  // data for the label becomes available, then we subscribe them.
  std::unordered_map<std::string, std::vector<ContextSubscriberLinkPtr>>
      pending_queries_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Repo);
};

}  // namespace maxwell
