// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include "apps/mozart/src/composer/resources/resource.h"

namespace mozart {
namespace composer {

class Node;
typedef ftl::RefPtr<Node> NodePtr;

class Node : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  bool AddChild(NodePtr child_node);
  bool AddPart(NodePtr part_node);

  // This is a static method so that it can be passed a NodePtr&, to facilitate
  // look-up in the node's parent.  No-op if node has no parent.
  static void Detach(const NodePtr& node_to_detach_from_parent);

 protected:
  Node(Session* session, const ResourceTypeInfo& type_info);

 private:
  bool is_part_ = false;
  Node* parent_ = nullptr;
  std::set<NodePtr> children_;
  std::set<NodePtr> parts_;
};

}  // namespace composer
}  // namespace mozart
