// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/escher/geometry/transform.h"

namespace scene_manager {

class Node;
using NodePtr = fxl::RefPtr<Node>;

// Node is an abstract base class for all the concrete node types listed in
// scene/services/nodes.fidl.
class Node : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  virtual ~Node() override;

  bool AddChild(NodePtr child_node);
  bool DetachChildren();
  bool AddPart(NodePtr part_node);

  bool SetTagValue(uint32_t tag_value);
  uint32_t tag_value() const { return tag_value_; }

  bool SetTransform(const escher::Transform& transform);
  bool SetTranslation(const escher::vec3& translation);
  bool SetScale(const escher::vec3& scale);
  bool SetRotation(const escher::quat& rotation);
  bool SetAnchor(const escher::vec3& anchor);
  bool SetClipToSelf(bool clip_to_self);
  bool SetHitTestBehavior(scenic::HitTestBehavior behavior);

  const escher::mat4& GetGlobalTransform() const;

  const escher::Transform& transform() const { return transform_; }
  const escher::vec3& translation() const { return transform_.translation; }
  const escher::vec3& scale() const { return transform_.scale; }
  const escher::quat& rotation() const { return transform_.rotation; }
  const escher::vec3& anchor() const { return transform_.anchor; }
  bool clip_to_self() const { return clip_to_self_; }
  scenic::HitTestBehavior hit_test_behavior() const {
    return hit_test_behavior_;
  }

  // The node's metrics as reported to the session listener.
  scenic::Metrics reported_metrics() const { return reported_metrics_; }
  void set_reported_metrics(scenic::Metrics metrics) {
    reported_metrics_ = metrics;
  }

  // |Resource|, DetachOp.
  bool Detach() override;

  Node* parent() const { return parent_; }

  const std::vector<NodePtr>& children() const { return children_; }

  const std::vector<NodePtr>& parts() const { return parts_; }

  bool SetEventMask(uint32_t event_mask) override;

  void AddImport(Import* import) override;
  void RemoveImport(Import* import) override;

  // Computes the closest point of intersection between the ray's origin
  // and the front side of the node's own content, excluding its descendants.
  // Does not apply clipping.
  //
  // |out_distance| is set to the distance from the ray's origin to the
  // closest point of intersection in multiples of the ray's direction vector.
  //
  // Returns true if there is an intersection, otherwise returns false and
  // leaves |out_distance| unmodified.
  virtual bool GetIntersection(const escher::ray4& ray,
                               float* out_distance) const;

 protected:
  Node(Session* session,
       scenic::ResourceId node_id,
       const ResourceTypeInfo& type_info);

 private:
  void InvalidateGlobalTransform();
  void ComputeGlobalTransform() const;

  void ErasePart(Node* part);
  void EraseChild(Node* child);

  // Describes the manner in which a node is related to its parent.
  enum class ParentRelation { kNone, kChild, kPart, kImportDelegate };

  uint32_t tag_value_ = 0u;
  Node* parent_ = nullptr;
  ParentRelation parent_relation_ = ParentRelation::kNone;
  std::vector<NodePtr> children_;
  std::vector<NodePtr> parts_;

  escher::Transform transform_;
  mutable escher::mat4 global_transform_;
  mutable bool global_transform_dirty_ = true;
  bool clip_to_self_ = false;
  scenic::HitTestBehavior hit_test_behavior_ =
      scenic::HitTestBehavior::kDefault;
  scenic::Metrics reported_metrics_;
};

// Inline functions.

inline const escher::mat4& Node::GetGlobalTransform() const {
  if (global_transform_dirty_) {
    ComputeGlobalTransform();
    global_transform_dirty_ = false;
  }
  return global_transform_;
}

}  // namespace scene_manager
