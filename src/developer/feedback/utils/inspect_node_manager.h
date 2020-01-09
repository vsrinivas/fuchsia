// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_INSPECT_NODE_MANAGER_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_INSPECT_NODE_MANAGER_H_

#include <lib/inspect/cpp/vmo/types.h>

#include <map>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/string_view.h"

namespace feedback {

// Manage Inspect nodes, allowing access using paths relative to the inspect root. Nodes are
// created lazily upon request to get a node or one of its children.
class InspectNodeManager {
 public:
  InspectNodeManager(inspect::Node* root_node) : root_(root_node) {}

  // Get the Inspect node at the provided path, creating nodes along the way if need be.
  inspect::Node& Get(const std::string& path);

  // Remove an Inspect node at the provided path.
  //
  // Return false, if any node in the path doesn't exist.
  bool Remove(const std::string& path);

  // Replaces all backslashes in |input| with a character that will later be replaced with a
  // backslash when the string is written to Inspect.
  static std::string SanitizeString(std::string str);

 private:
  std::vector<std::string> SplitAndDesanitize(const std::string& path);

  class ManagedNode;

  class ManagedNodeBase {
   public:
    // Return true if the current node contains child.
    bool HasChild(const std::string& child) const;

    // Get a child of the current node. If the child doesn't exist, create it.
    ManagedNode& GetChild(const std::string& child);

    // Remove a child of the current node. If the child doesn't exist, return false.
    bool RemoveChild(const std::string& child);

    virtual inspect::Node& GetNode() = 0;

   protected:
    std::map<std::string, ManagedNode> children_;
  };

  class ManagedNode : public ManagedNodeBase {
   public:
    ManagedNode(inspect::Node node) : node_(std::move(node)) {}

    // Allow moving, disallow copying.
    ManagedNode(ManagedNode&& other) = default;
    ManagedNode& operator=(ManagedNode&& other) noexcept;
    ManagedNode(const ManagedNode& other) = delete;
    ManagedNode& operator=(const ManagedNode other) = delete;

    inspect::Node& GetNode() override { return node_; }

   private:
    inspect::Node node_;
  };

  class RootManagedNode : public ManagedNodeBase {
   public:
    RootManagedNode(inspect::Node* root_node) : node_(root_node) {}

    inspect::Node& GetNode() override { return *node_; }

   private:
    inspect::Node* node_;

    FXL_DISALLOW_COPY_AND_ASSIGN(RootManagedNode);
  };

  RootManagedNode root_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectNodeManager);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_INSPECT_NODE_MANAGER_H_
