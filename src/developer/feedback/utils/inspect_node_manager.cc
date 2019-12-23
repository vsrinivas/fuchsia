// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/inspect_node_manager.h"

#include <algorithm>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/split_string.h"

namespace feedback {
namespace {

// The ASCII bell character (0x07) will be used to replace all backslashes.
constexpr uint8_t kBackslashReplacement = 0x07;

void ReplaceCharInString(char to_replace, char replace_with, std::string* str) {
  for (size_t i = 0; i < str->size(); ++i) {
    if ((*str)[i] == to_replace) {
      (*str)[i] = replace_with;
    }
  }
}

}  // namespace

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

  const auto split_path = SplitAndDesanitize(path);

  for (auto child : split_path) {
    // Create the child if it doesn't exist, then get the child.
    node = &node->GetChild(child);
  }

  return node->GetNode();
}

bool InspectNodeManager::Remove(const std::string& path) {
  ManagedNodeBase* node = &root_;

  const auto split_path = SplitAndDesanitize(path);

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

std::string InspectNodeManager::SanitizeString(std::string str) {
  ReplaceCharInString('/', kBackslashReplacement, &str);
  return str;
}

std::vector<std::string> InspectNodeManager::SplitAndDesanitize(const std::string& path) {
  std::vector<std::string> split_path =
      SplitStringCopy(path, "/", kTrimWhitespace, kSplitWantNonEmpty);

  for (auto& part : split_path) {
    ReplaceCharInString(kBackslashReplacement, '/', &part);
  }

  return split_path;
}

}  // namespace feedback
