// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/inspect_node_manager.h"

#include <algorithm>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"

namespace feedback {

using fxl::kKeepWhitespace;
using fxl::kSplitWantNonEmpty;
using inspect::Node;
using internal::SplitPathOrDie;

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

Node* InspectNodeManager::GetOrDie(const std::string& path) {
  ManagedNodeBase* parent = &root_;
  for (const auto& child : SplitPathOrDie(path)) {
    // Create the child if it doesn't exist, then get the child.
    parent = &parent->GetChild(child.ToString());
  }

  return parent->GetNode();
}

namespace internal {

std::vector<fxl::StringView> SplitPathOrDie(const std::string& path) {
  FXL_CHECK(!path.empty()) << "path cannot be empty";
  FXL_CHECK(path[0] == '/') << "path must start with '\'";
  FXL_CHECK(path.find(' ') == std::string::npos) << "path cannot contain whitespace";

  return fxl::SplitString(path, "/", kKeepWhitespace, kSplitWantNonEmpty);
}

}  // namespace internal
}  // namespace feedback
