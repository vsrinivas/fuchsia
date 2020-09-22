// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

namespace {

constexpr ResourceTypeFlags kHasChildren = ResourceType::kEntityNode | ResourceType::kOpacityNode |
                                           ResourceType::kScene | ResourceType::kViewNode;
constexpr ResourceTypeFlags kHasTransform = ResourceType::kClipNode | ResourceType::kEntityNode |
                                            ResourceType::kOpacityNode | ResourceType::kScene |
                                            ResourceType::kShapeNode | ResourceType::kViewHolder |
                                            ResourceType::kViewNode;
constexpr ResourceTypeFlags kHasClip = ResourceType::kEntityNode | ResourceType::kViewHolder;

}  // anonymous namespace

const ResourceTypeInfo Node::kTypeInfo = {ResourceType::kNode, "Node"};

Node::Node(Session* session, SessionId session_id, ResourceId node_id,
           const ResourceTypeInfo& type_info)
    : Resource(session, session_id, node_id, type_info) {
  FX_DCHECK(type_info.IsKindOf(Node::kTypeInfo));
}

Node::~Node() {
  ForEachChildFrontToBack(*this, [](Node* node) {
    FX_DCHECK(node->parent_relation_ != ParentRelation::kNone);
    // Detach without affecting parent Node (because thats us) or firing the
    // on_detached_cb_ (because that shouldn't be up to us).
    node->DetachInternal();
  });
}

bool Node::SetEventMask(uint32_t event_mask) {
  if (!Resource::SetEventMask(event_mask))
    return false;

  // If the client unsubscribed from the event, ensure that we will deliver
  // fresh metrics next time they subscribe.
  if (!(event_mask & ::fuchsia::ui::gfx::kMetricsEventMask)) {
    reported_metrics_ = ::fuchsia::ui::gfx::Metrics();
  }
  return true;
}

bool Node::CanAddChild(NodePtr child_node) { return type_flags() & kHasChildren; }

bool Node::AddChild(NodePtr child_node, ErrorReporter* error_reporter) {
  if (child_node->type_flags() & ResourceType::kScene) {
    error_reporter->ERROR() << "scenic::gfx::Node::AddChild(): node of type '"
                            << child_node->type_name() << "' cannot be a child of another node.";
    return false;
  }
  if (!CanAddChild(child_node)) {
    error_reporter->ERROR() << "scenic::gfx::Node::AddChild(): node of type '" << type_name()
                            << "' cannot have children of type " << child_node->type_name();
    return false;
  }

  if (child_node->parent_relation_ == ParentRelation::kChild && child_node->parent_ == this) {
    return true;  // no change
  }

  // Detach and re-attach Node to us.
  child_node->Detach(error_reporter);
  child_node->SetParent(this, ParentRelation::kChild);
  children_.push_back(std::move(child_node));
  return true;
}

void Node::SetParent(Node* parent, ParentRelation relation) {
  FX_DCHECK(parent_ == nullptr);
  // A Scene node should always be a root node, and never a child.
  FX_DCHECK(!(type_flags() & ResourceType::kScene)) << "A Scene node cannot have a parent";

  parent_ = parent;
  parent_relation_ = relation;
  RefreshScene(parent_->scene());
}

bool Node::Detach(ErrorReporter* error_reporter) {
  if (parent_) {
    switch (parent_relation_) {
      case ParentRelation::kChild:
        parent_->EraseChild(this);
        break;
      case ParentRelation::kNone:
        FX_NOTREACHED();
        break;
    }
    DetachInternal();
  }
  return true;
}

bool Node::DetachChildren(ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasChildren)) {
    error_reporter->ERROR() << "scenic::gfx::Node::DetachChildren(): node of type '" << type_name()
                            << "' cannot have children.";
    return false;
  }

  // To be safe, avoid invalid iterations over detached children by moving the
  // vector first.
  auto children_to_detach = std::move(children_);
  children_.clear();
  for (auto& child : children_to_detach) {
    // Detach without affecting parent Node (because thats us) or firing the
    // on_detached_cb_ (because that shouldn't be up to us).
    child->DetachInternal();
  }

  return true;
}

bool Node::SetTransform(const escher::Transform& transform, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetTransform(): node of type " << type_name()
                            << " cannot have transform set.";
    return false;
  }
  transform_ = transform;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetTranslation(const escher::vec3& translation, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetTranslation(): node of type " << type_name()
                            << " cannot have translation set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kTranslation);

  transform_.translation = translation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetTranslation(Vector3VariablePtr translation_variable, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetTranslation(): node of type " << type_name()
                            << " cannot have translation set.";
    return false;
  }

  bound_variables_[NodeProperty::kTranslation] =
      std::make_unique<Vector3VariableBinding>(translation_variable, [this](escher::vec3 value) {
        transform_.translation = value;
        InvalidateGlobalTransform();
      });
  return true;
}

bool Node::SetScale(const escher::vec3& scale, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetScale(): node of type " << type_name()
                            << " cannot have scale set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kScale);
  transform_.scale = scale;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetScale(Vector3VariablePtr scale_variable, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetScale(): node of type " << type_name()
                            << " cannot have scale set.";
    return false;
  }
  bound_variables_[NodeProperty::kScale] =
      std::make_unique<Vector3VariableBinding>(scale_variable, [this](escher::vec3 value) {
        transform_.scale = value;
        InvalidateGlobalTransform();
      });
  return true;
}

bool Node::SetRotation(const escher::quat& rotation, ErrorReporter* error_reporter) {
  // TODO(fxbug.dev/24183): Safer handling of quats.  Put DCHECK here; validation
  // should happen elsewhere, before reaching this point.
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetRotation(): node of type " << type_name()
                            << " cannot have rotation set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kRotation);
  transform_.rotation = rotation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetRotation(QuaternionVariablePtr rotation_variable, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetRotation(): node of type " << type_name()
                            << " cannot have rotation set.";
    return false;
  }
  bound_variables_[NodeProperty::kRotation] =
      std::make_unique<QuaternionVariableBinding>(rotation_variable, [this](escher::quat value) {
        transform_.rotation = value;
        InvalidateGlobalTransform();
      });
  return true;
}

bool Node::SetAnchor(const escher::vec3& anchor, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetAnchor(): node of type " << type_name()
                            << " cannot have anchor set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kAnchor);
  transform_.anchor = anchor;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetAnchor(Vector3VariablePtr anchor_variable, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetAnchor(): node of type " << type_name()
                            << " cannot have anchor set.";
    return false;
  }
  bound_variables_[NodeProperty::kAnchor] =
      std::make_unique<Vector3VariableBinding>(anchor_variable, [this](escher::vec3 value) {
        transform_.anchor = value;
        InvalidateGlobalTransform();
      });
  return true;
}

bool Node::SetClipToSelf(bool clip_to_self, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasClip)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetClipToSelf(): node of type " << type_name()
                            << " cannot have clip params set.";
    return false;
  }
  clip_to_self_ = clip_to_self;
  return true;
}

bool Node::SetClipPlanesFromBBox(const escher::BoundingBox& box, ErrorReporter* error_reporter) {
  return SetClipPlanes(box.CreatePlanes(), error_reporter);
}

bool Node::SetClipPlanes(std::vector<escher::plane3> clip_planes, ErrorReporter* error_reporter) {
  if (!(type_flags() & kHasClip)) {
    error_reporter->ERROR() << "scenic::gfx::Node::SetClipPlanes(): node of type " << type_name()
                            << " cannot have clip params set.";
    return false;
  }
  clip_planes_ = std::move(clip_planes);
  return true;
}

bool Node::SetHitTestBehavior(::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior) {
  hit_test_behavior_ = hit_test_behavior;
  return true;
}

bool Node::SetSemanticVisibility(bool visible) {
  semantically_visible_ = visible;
  return true;
}

bool Node::SendSizeChangeHint(float width_change_factor, float height_change_factor) {
  if (event_mask() & ::fuchsia::ui::gfx::kSizeChangeHintEventMask) {
    auto event = ::fuchsia::ui::gfx::Event();
    event.set_size_change_hint(::fuchsia::ui::gfx::SizeChangeHintEvent());
    event.size_change_hint().node_id = id();
    event.size_change_hint().width_change_factor = width_change_factor;
    event.size_change_hint().height_change_factor = height_change_factor;
    event_reporter()->EnqueueEvent(std::move(event));
  }

  ForEachChildFrontToBack(*this, [width_change_factor, height_change_factor](Node* node) {
    node->SendSizeChangeHint(width_change_factor, height_change_factor);
  });
  return true;
}

bool Node::ClipsRay(const escher::ray4& ray) const {
  for (auto& plane : clip_planes_) {
    if (!escher::IntersectRayPlane(ray, plane, nullptr)) {
      return false;
    }
  }
  return true;
}

Node::IntersectionInfo Node::GetIntersection(const escher::ray4& ray,
                                             const IntersectionInfo& parent_intersection) const {
  // This method shouldn't have been called if the parent didn't want to traverse into children.
  FX_DCHECK(parent_intersection.continue_with_children);
  IntersectionInfo result;
  result.interval = parent_intersection.interval;
  result.continue_with_children = true;
  return result;
}

void Node::InvalidateGlobalTransform() {
  if (!global_transform_dirty_) {
    global_transform_dirty_ = true;
    ForEachChildFrontToBack(*this, [](Node* node) { node->InvalidateGlobalTransform(); });
  }
}

void Node::ComputeGlobalTransform() const {
  if (parent_) {
    global_transform_ = parent_->GetGlobalTransform() * static_cast<escher::mat4>(transform_);
  } else {
    global_transform_ = static_cast<escher::mat4>(transform_);
  }
}

void Node::EraseChild(Node* child) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [child](const NodePtr& ptr) { return child == ptr.get(); });
  FX_DCHECK(it != children_.end());
  children_.erase(it);
}

void Node::DetachInternal() {
  parent_relation_ = ParentRelation::kNone;
  parent_ = nullptr;
  if (!(type_flags() & ResourceType::kScene)) {
    RefreshScene(nullptr);
  }
  InvalidateGlobalTransform();
}

void Node::RefreshScene(Scene* new_scene) {
  if (new_scene == scene_) {
    // Scene is already set on this node and all its children.
    return;
  }

  scene_ = new_scene;
  OnSceneChanged();

  ForEachChildFrontToBack(*this, [this](Node* node) { node->RefreshScene(scene_); });
}

ViewPtr Node::FindOwningView() const {
  if (parent()) {
    return parent()->FindOwningView();
  }
  return nullptr;
}

}  // namespace gfx
}  // namespace scenic_impl
