// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/nodes/node.h"

#include "apps/mozart/src/composer/util/error_reporter.h"

namespace mozart {
namespace composer {

namespace {

constexpr ResourceTypeFlags kHasChildren = ResourceType::kEntityNode;
constexpr ResourceTypeFlags kHasParts =
    ResourceType::kEntityNode | ResourceType::kClipNode;

}  // anonymous namespace

const ResourceTypeInfo Node::kTypeInfo = {ResourceType::kNode, "Node"};

Node::Node(Session* session, const ResourceTypeInfo& type_info)
    : Resource(session, type_info) {
  FTL_DCHECK(type_info.IsKindOf(Node::kTypeInfo));
}

bool Node::AddChild(NodePtr child_node) {
  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR() << "composer::Node::AddChild(): node of type "
                              << type_name() << " cannot have children.";
    return false;
  }

  // Remove child from current parent, if necessary.
  if (auto parent = child_node->parent_) {
    if (this == parent && !child_node->is_part_) {
      // Node is already our child.
      return true;
    }
    // Remove child from parent.
    Detach(child_node);
  }

  // Add child to its new parent (i.e. us).
  child_node->is_part_ = false;
  child_node->parent_ = this;
  auto insert_result = children_.insert(std::move(child_node));
  FTL_DCHECK(insert_result.second);

  return true;
}

bool Node::AddPart(NodePtr part_node) {
  if (!(type_flags() & kHasParts)) {
    error_reporter()->ERROR() << "composer::Node::AddPart(): node of type "
                              << type_name() << " cannot have parts.";
    return false;
  }

  // Remove part from current parent, if necessary.
  if (auto parent = part_node->parent_) {
    if (this == parent && part_node->is_part_) {
      // Node is already our child.
      return true;
    }
    Detach(part_node);
  }

  // Add part to its new parent (i.e. us).
  part_node->is_part_ = true;
  part_node->parent_ = this;
  auto insert_result = children_.insert(std::move(part_node));
  FTL_DCHECK(insert_result.second);

  return true;
}

void Node::Detach(const NodePtr& node_to_detach_from_parent) {
  FTL_DCHECK(node_to_detach_from_parent);
  if (auto parent = node_to_detach_from_parent->parent_) {
    auto& container = node_to_detach_from_parent->is_part_ ? parent->parts_
                                                           : parent->children_;
    size_t removed_count = container.erase(node_to_detach_from_parent);
    FTL_DCHECK(removed_count == 1);  // verify parent-child invariant
    node_to_detach_from_parent->parent_ = nullptr;
  }
}

}  // namespace composer
}  // namespace mozart
