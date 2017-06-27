// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>

#include "apps/mozart/src/scene/resources/resource.h"
#include "lib/escher/escher/geometry/transform.h"

namespace mozart {
namespace scene {

class Node;
using NodePtr = ftl::RefPtr<Node>;

// Node is an abstract base class for all the concrete node types listed in
// scene/services/nodes.fidl.
class Node : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  virtual ~Node() override;

  ResourceId resource_id() const { return resource_id_; }

  bool AddChild(NodePtr child_node);
  bool AddPart(NodePtr part_node);

  bool SetTransform(const escher::Transform& transform);
  const escher::mat4& GetGlobalTransform() const;
  const escher::Transform& transform() const { return transform_; }

  // This is a static method so that it can be passed a NodePtr&, to facilitate
  // look-up in the node's parent.  No-op if node has no parent.  Always returns
  // true.
  static bool Detach(const NodePtr& node_to_detach_from_parent);

  Node* parent() const { return parent_; }

  const std::set<NodePtr>& children() const { return children_; }

  const std::set<NodePtr>& parts() const { return parts_; }

  /// Convert a point that is in the coordinate space of the supplied node into
  /// the coordinate space of the callee.
  escher::vec2 ConvertPointFromNode(const escher::vec2& point,
                                    const Node& node) const;

  /// Returns if the given point (that is already in the coordinate space of the
  /// node being queried) lies within its bounds.
  virtual bool ContainsPoint(const escher::vec2& point) const;

  void AddImport(Import* import) override;
  void RemoveImport(Import* import) override;

 protected:
  Node(Session* session, ResourceId node_id, const ResourceTypeInfo& type_info);

  /// Applies the lambda on the descendents of the given node. Returning false
  /// from the lambda indicates that no further iteration is necessary. In that
  /// case, the lambda will not be called again.
  void ApplyOnDescendants(std::function<bool(const Node&)> applier) const;

 private:
  void InvalidateGlobalTransform();
  void ComputeGlobalTransform() const;

  const ResourceId resource_id_;
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

}  // namespace scene
}  // namespace mozart
