// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/inspect_node_manager.h"

#include <algorithm>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"

namespace feedback {

using fxl::kSplitWantNonEmpty;
using fxl::kTrimWhitespace;
using fxl::SplitStringCopy;
using inspect::Node;

bool InspectNodeManager::ManagedNodeBase::HasChild(const std::string& child) const {
  return children_.find(child) != children_.end();
}

InspectNodeManager::ManagedNode& InspectNodeManager::ManagedNodeBase::GetChild(
    const std::string& child) {
  if (!HasChild(child)) {
    children_.emplace(child, ManagedNode(GetNode().CreateChild(child)));
  }
  return children_.at(child);
}

bool InspectNodeManager::ManagedNodeBase::RemoveChild(const std::string& child) {
  return children_.erase(child) != 0;
}

Node& InspectNodeManager::Get(const std::string& path) {
  ManagedNodeBase* node = &root_;
  for (const auto& child : SplitStringCopy(path, "/", kTrimWhitespace, kSplitWantNonEmpty)) {
    // Create the child if it doesn't exist, then get the child.
    node = &node->GetChild(child);
  }

  return node->GetNode();
}

bool InspectNodeManager::Remove(const std::string& path) {
  ManagedNodeBase* node = &root_;

  const std::vector<std::string> split_path =
      SplitStringCopy(path, "/", kTrimWhitespace, kSplitWantNonEmpty);

  // Find the parent node.
  for (size_t i = 0; i < split_path.size() - 1; ++i) {
    // Return early if a node in the path doesn't exist.
    if (!node->HasChild(split_path[i])) {
      return false;
    }
    node = &node->GetChild(split_path[i]);
  }

  return node->RemoveChild(split_path.back());
}

}  // namespace feedback
