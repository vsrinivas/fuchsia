// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_NODES_NODE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_NODES_NODE_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "garnet/lib/ui/gfx/resources/nodes/variable_binding.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/resources/variable.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/transform.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Scene;
class View;

using NodePtr = fxl::RefPtr<Node>;
using ViewPtr = fxl::RefPtr<View>;

// Node is an abstract base class for all the concrete node types listed in
// scene/services/nodes.fidl.
class Node : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  virtual ~Node() override;

  bool AddChild(NodePtr child_node);
  bool AddPart(NodePtr part_node);
  bool DetachChildren();

  bool SetTagValue(uint32_t tag_value);
  uint32_t tag_value() const { return tag_value_; }

  bool SetTransform(const escher::Transform& transform);
  bool SetTranslation(const escher::vec3& translation);
  bool SetTranslation(Vector3VariablePtr translation);
  bool SetScale(const escher::vec3& scale);
  bool SetScale(Vector3VariablePtr scale);
  bool SetRotation(const escher::quat& rotation);
  bool SetRotation(QuaternionVariablePtr rotation);
  bool SetAnchor(const escher::vec3& anchor);
  bool SetAnchor(Vector3VariablePtr anchor);
  bool SetClipToSelf(bool clip_to_self);
  bool SetClipPlanes(std::vector<escher::plane3> clip_planes);
  bool SetClipPlanesFromBBox(const escher::BoundingBox& box);
  bool SetHitTestBehavior(::fuchsia::ui::gfx::HitTestBehavior behavior);
  bool SendSizeChangeHint(float width_change_factor,
                          float height_change_factor);

  const escher::mat4& GetGlobalTransform() const;

  const escher::Transform& transform() const { return transform_; }
  const escher::vec3& translation() const { return transform_.translation; }
  const escher::vec3& scale() const { return transform_.scale; }
  const escher::quat& rotation() const { return transform_.rotation; }
  const escher::vec3& anchor() const { return transform_.anchor; }
  bool clip_to_self() const { return clip_to_self_; }
  const std::vector<escher::plane3>& clip_planes() const {
    return clip_planes_;
  }
  ::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior() const {
    return hit_test_behavior_;
  }

  // The node's metrics as reported to the session listener.
  ::fuchsia::ui::gfx::Metrics reported_metrics() const {
    return reported_metrics_;
  }
  void set_reported_metrics(::fuchsia::ui::gfx::Metrics metrics) {
    reported_metrics_ = metrics;
  }

  // |Resource|, DetachCmd.
  bool Detach() override;

  Node* parent() const { return parent_; }

  // Each Node caches its containing Scene.  This is nullptr if the Node is not
  // part of a Scene.
  Scene* scene() const { return scene_; }

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

  // Walk up tree until we find the responsible View; otherwise return nullptr.
  // N.B. Typically the view and node are in the same session, but it's possible
  // to have them inhabit different sessions.
  virtual ViewPtr FindOwningView() const;

 protected:
  Node(Session* session, ResourceId node_id, const ResourceTypeInfo& type_info);

  // Returns whether or not this node can add the |child_node| as a child.
  virtual bool CanAddChild(NodePtr child_node);
  // Triggered on the node when the node's | scene_ | has changed, before its
  // children are updated with the new scene.
  virtual void OnSceneChanged() {}

  // Protected so that Scene Node can set itself as a Scene.
  Scene* scene_ = nullptr;

 private:
  // Describes the manner in which a node is related to its parent.
  enum class ParentRelation { kNone, kChild, kPart, kImportDelegate };

  // Identifies a specific spatial property.
  enum NodeProperty { kTranslation, kScale, kRotation, kAnchor };

  void InvalidateGlobalTransform();
  void ComputeGlobalTransform() const;

  void SetParent(Node* parent, ParentRelation relation);
  void EraseChild(Node* part);
  void ErasePart(Node* part);

  // Reset the parent and any dependent properties like scene and global
  // transform.  This allows "detaching" from the parent without affecting the
  // parent itself or firing the on_detached callback (which affects the
  // containing View).
  //
  // Only called internally by the Node on its children, never externally.
  void DetachInternal();
  void RefreshScene(Scene* new_scene);

  uint32_t tag_value_ = 0u;

  Node* parent_ = nullptr;
  ParentRelation parent_relation_ = ParentRelation::kNone;
  // TODO(SCN-1299) Split out child behavior into ContainerNode class.
  std::vector<NodePtr> children_;
  std::vector<NodePtr> parts_;

  std::unordered_map<NodeProperty, std::unique_ptr<VariableBinding>>
      bound_variables_;

  escher::Transform transform_;
  mutable escher::mat4 global_transform_;
  mutable bool global_transform_dirty_ = true;
  bool clip_to_self_ = false;
  std::vector<escher::plane3> clip_planes_;
  ::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior_ =
      ::fuchsia::ui::gfx::HitTestBehavior::kDefault;
  ::fuchsia::ui::gfx::Metrics reported_metrics_;
};

// Inline functions.

inline const escher::mat4& Node::GetGlobalTransform() const {
  if (global_transform_dirty_) {
    ComputeGlobalTransform();
    global_transform_dirty_ = false;
  }
  return global_transform_;
}

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_NODES_NODE_H_
