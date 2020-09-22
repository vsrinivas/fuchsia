// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_NODE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_NODE_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/interval.h"
#include "src/ui/lib/escher/geometry/transform.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/variable_binding.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/variable.h"

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
  // Per-node intersection data for HitTester object.
  //
  // The hit ray implicitly defines a 1-dimensional space ("ray space") with origin at the ray
  // origin and unit length defined by the ray direction vector (which is not necessarily a unit
  // vector).
  //
  // Hit testing is done in part by projecting 3-dimensional geometry onto the hit ray. The distance
  // of the hit is its ray-space coordinate. This allows for direct comparison of hit distance
  // amongst objects in different coordinate systems without needing further transformation.
  struct IntersectionInfo {
    // Maximum possible hit distance allowed, in ray space.
    static constexpr float kMaximumDistance = 1000000000.f;

    // True if the ray intersects the given node.
    bool did_hit = false;

    // True if hit tester should traverse the node's descendants.
    bool continue_with_children = true;

    // Hit coordinate, in ray space.
    float distance = 0;

    // Min and max extent of what can be hit, in ray space.
    escher::Interval interval = escher::Interval(0.f, kMaximumDistance);
  };

  static const ResourceTypeInfo kTypeInfo;

  virtual ~Node() override;

  bool AddChild(NodePtr child_node, ErrorReporter* error_reporter);
  bool DetachChildren(ErrorReporter* error_reporter);

  bool SetTransform(const escher::Transform& transform, ErrorReporter* error_reporter);
  bool SetTranslation(const escher::vec3& translation, ErrorReporter* error_reporter);
  bool SetTranslation(Vector3VariablePtr translation, ErrorReporter* error_reporter);
  bool SetScale(const escher::vec3& scale, ErrorReporter* error_reporter);
  bool SetScale(Vector3VariablePtr scale, ErrorReporter* error_reporter);
  bool SetRotation(const escher::quat& rotation, ErrorReporter* error_reporter);
  bool SetRotation(QuaternionVariablePtr rotation, ErrorReporter* error_reporter);
  bool SetAnchor(const escher::vec3& anchor, ErrorReporter* error_reporter);
  bool SetAnchor(Vector3VariablePtr anchor, ErrorReporter* error_reporter);
  bool SetClipToSelf(bool clip_to_self, ErrorReporter* error_reporter);
  bool SetClipPlanes(std::vector<escher::plane3> clip_planes, ErrorReporter* error_reporter);
  bool SetClipPlanesFromBBox(const escher::BoundingBox& box, ErrorReporter* error_reporter);
  bool SetHitTestBehavior(::fuchsia::ui::gfx::HitTestBehavior behavior);
  bool SetSemanticVisibility(bool visible);
  bool SendSizeChangeHint(float width_change_factor, float height_change_factor);

  const escher::mat4& GetGlobalTransform() const;

  const escher::Transform& transform() const { return transform_; }
  const escher::vec3& translation() const { return transform_.translation; }
  const escher::vec3& scale() const { return transform_.scale; }
  const escher::quat& rotation() const { return transform_.rotation; }
  const escher::vec3& anchor() const { return transform_.anchor; }
  bool clip_to_self() const { return clip_to_self_; }
  const std::vector<escher::plane3>& clip_planes() const { return clip_planes_; }
  ::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior() const { return hit_test_behavior_; }
  bool semantically_visible() const { return semantically_visible_; }

  // The node's metrics as reported to the session listener.
  ::fuchsia::ui::gfx::Metrics reported_metrics() const { return reported_metrics_; }
  void set_reported_metrics(::fuchsia::ui::gfx::Metrics metrics) { reported_metrics_ = metrics; }

  // |Resource|, DetachCmd.
  bool Detach(ErrorReporter* error_reporter) override;

  Node* parent() const { return parent_; }

  // Each Node caches its containing Scene.  This is nullptr if the Node is not
  // part of a Scene.
  Scene* scene() const { return scene_; }

  const std::vector<NodePtr>& children() const { return children_; }

  bool SetEventMask(uint32_t event_mask) override;

  // Tests if the ray is clipped by the node's clip planes.
  bool ClipsRay(const escher::ray4& ray) const;

  // Computes the closest point of intersection between the ray's origin and the front side of the
  // node's own content, excluding its descendants. Does not apply clipping.
  //
  // The ray is interpreted in the coordinate space of the node.
  virtual IntersectionInfo GetIntersection(const escher::ray4& ray,
                                           const IntersectionInfo& parent_intersection) const;

  // Walk up tree until we find the responsible View; otherwise return nullptr.
  // N.B. Typically the view and node are in the same session, but it's possible
  // to have them inhabit different sessions.
  virtual ViewPtr FindOwningView() const;

 protected:
  Node(Session* session, SessionId session_id, ResourceId node_id,
       const ResourceTypeInfo& type_info);

  // Returns whether or not this node can add the |child_node| as a child.
  virtual bool CanAddChild(NodePtr child_node);
  // Triggered on the node when the node's | scene_ | has changed, before its
  // children are updated with the new scene.
  virtual void OnSceneChanged() {}

  // Protected so that Scene Node can set itself as a Scene.
  Scene* scene_ = nullptr;

 private:
  // Describes the manner in which a node is related to its parent.
  enum class ParentRelation { kNone, kChild };

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

  Node* parent_ = nullptr;
  ParentRelation parent_relation_ = ParentRelation::kNone;
  // TODO(fxbug.dev/24497) Split out child behavior into ContainerNode class.
  std::vector<NodePtr> children_;

  std::unordered_map<NodeProperty, std::unique_ptr<VariableBinding>> bound_variables_;

  escher::Transform transform_;
  mutable escher::mat4 global_transform_;
  mutable bool global_transform_dirty_ = true;
  bool clip_to_self_ = false;
  std::vector<escher::plane3> clip_planes_;
  ::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior_ =
      ::fuchsia::ui::gfx::HitTestBehavior::kDefault;
  bool semantically_visible_ = true;
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

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_NODE_H_
