// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/parent_set_collector.h"

namespace dfv2 {

void ParentSetCollector::AddNode(uint32_t index, std::weak_ptr<Node> node) {
  ZX_ASSERT(index < size_);
  parents_[index] = std::move(node);
}

void ParentSetCollector::RemoveNode(uint32_t index) {
  ZX_ASSERT(index < size_);
  parents_[index] = std::weak_ptr<Node>();
}

std::optional<std::vector<Node*>> ParentSetCollector::GetIfComplete() {
  std::vector<Node*> parents;
  for (auto& node : parents_) {
    if (auto parent = node.lock()) {
      parents.push_back(parent.get());
    } else {
      // We are missing a node or it has been removed.
      return std::nullopt;
    }
  }

  return parents;
}

bool ParentSetCollector::ContainsNode(uint32_t index) const {
  ZX_ASSERT(index < size_);
  return !parents_[index].expired();
}

}  // namespace dfv2
