// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include "apps/mozart/src/composer/resources/resource.h"
#include "lib/escher/escher/geometry/transform.h"

namespace mozart {
namespace composer {

class Node;
typedef ftl::RefPtr<Node> NodePtr;

// Node is an abstract base class for all the concrete node types listed in
// composer/services/nodes.fidl.
class Node : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  bool AddChild(NodePtr child_node);
  bool AddPart(NodePtr part_node);

  bool SetTransform(const escher::Transform& transform);
  const escher::mat4& GetGlobalTransform() const;

  // This is a static method so that it can be passed a NodePtr&, to facilitate
  // look-up in the node's parent.  No-op if node has no parent.  Always returns
  // true.
  static bool Detach(const NodePtr& node_to_detach_from_parent);

  Node* parent() const { return parent_; }

 protected:
  Node(Session* session, const ResourceTypeInfo& type_info);

 private:
  void InvalidateGlobalTransform();
  void ComputeGlobalTransform() const;

  bool is_part_ = false;
  Node* parent_ = nullptr;
  std::set<NodePtr> children_;
  std::set<NodePtr> parts_;

  escher::Transform transform_;
  mutable escher::mat4 global_transform_;
  mutable bool global_transform_dirty_ = true;
};

// Inline functions.

inline const escher::mat4& Node::GetGlobalTransform() const {
  if (global_transform_dirty_) {
    ComputeGlobalTransform();
    global_transform_dirty_ = false;
  }
  return global_transform_;
}

}  // namespace composer
}  // namespace mozart
