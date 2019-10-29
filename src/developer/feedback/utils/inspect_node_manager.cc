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
    children_.emplace(child, ManagedNode(GetNode()->CreateChild(child)));
  }
  return children_.at(child);
}

Node* InspectNodeManager::Get(const std::string& path) {
  ManagedNodeBase* parent = &root_;
  for (const auto& child : SplitStringCopy(path, "/", kTrimWhitespace, kSplitWantNonEmpty)) {
    // Create the child if it doesn't exist, then get the child.
    parent = &parent->GetChild(child);
  }

  return parent->GetNode();
}

}  // namespace feedback
