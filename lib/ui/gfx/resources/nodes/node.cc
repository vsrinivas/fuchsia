// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "garnet/lib/ui/gfx/resources/nodes/node.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/nodes/traversal.h"

#include "lib/escher/geometry/types.h"

namespace scenic {
namespace gfx {

namespace {

constexpr ResourceTypeFlags kHasChildren = ResourceType::kEntityNode |
                                           ResourceType::kOpacityNode |
                                           ResourceType::kScene;
constexpr ResourceTypeFlags kHasParts = ResourceType::kEntityNode |
                                        ResourceType::kOpacityNode |
                                        ResourceType::kClipNode;
constexpr ResourceTypeFlags kHasTransform =
    ResourceType::kClipNode | ResourceType::kEntityNode |
    ResourceType::kOpacityNode | ResourceType::kScene |
    ResourceType::kShapeNode;
constexpr ResourceTypeFlags kHasClip = ResourceType::kEntityNode;

}  // anonymous namespace

const ResourceTypeInfo Node::kTypeInfo = {ResourceType::kNode, "Node"};

Node::Node(Session* session, scenic::ResourceId node_id,
           const ResourceTypeInfo& type_info)
    : Resource(session, node_id, type_info) {
  FXL_DCHECK(type_info.IsKindOf(Node::kTypeInfo));
}

Node::~Node() {
  ForEachDirectDescendantFrontToBack(*this, [](Node* node) {
    FXL_DCHECK(node->parent_relation_ != ParentRelation::kNone);
    node->parent_relation_ = ParentRelation::kNone;
    node->parent_ = nullptr;
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

bool Node::AddChild(NodePtr child_node) {
  // TODO(MZ-130): Some node types (e.g. Scenes) cannot be reparented. We must
  // add verification to reject such operations.

  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::AddChild(): node of type '"
                              << type_name() << "' cannot have children.";
    return false;
  }

  if (child_node->parent_relation_ == ParentRelation::kChild &&
      child_node->parent_ == this) {
    return true;  // no change
  }
  child_node->Detach();

  // Add child to its new parent (i.e. us).
  child_node->parent_relation_ = ParentRelation::kChild;
  child_node->parent_ = this;
  child_node->InvalidateGlobalTransform();
  children_.push_back(std::move(child_node));
  return true;
}

bool Node::AddPart(NodePtr part_node) {
  if (!(type_flags() & kHasParts)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::AddPart(): node of type "
                              << type_name() << " cannot have parts.";
    return false;
  }

  if (part_node->parent_relation_ == ParentRelation::kPart &&
      part_node->parent_ == this) {
    return true;  // no change
  }
  part_node->Detach();

  // Add part to its new parent (i.e. us).
  part_node->parent_relation_ = ParentRelation::kPart;
  part_node->parent_ = this;
  part_node->InvalidateGlobalTransform();
  parts_.push_back(std::move(part_node));
  return true;
}

bool Node::Detach() {
  if (parent_) {
    switch (parent_relation_) {
      case ParentRelation::kChild:
        parent_->EraseChild(this);
        break;
      case ParentRelation::kPart:
        parent_->ErasePart(this);
        break;
      case ParentRelation::kImportDelegate:
        error_reporter()->ERROR() << "An imported node cannot be detached.";
        return false;
      case ParentRelation::kNone:
        FXL_NOTREACHED();
        break;
    }

    parent_relation_ = ParentRelation::kNone;
    parent_ = nullptr;
    InvalidateGlobalTransform();
  }
  return true;
}

void Node::ErasePart(Node* part) {
  auto it =
      std::find_if(parts_.begin(), parts_.end(),
                   [part](const NodePtr& ptr) { return part == ptr.get(); });
  FXL_DCHECK(it != parts_.end());
  parts_.erase(it);
}

void Node::EraseChild(Node* child) {
  auto it =
      std::find_if(children_.begin(), children_.end(),
                   [child](const NodePtr& ptr) { return child == ptr.get(); });
  FXL_DCHECK(it != children_.end());
  children_.erase(it);
}

bool Node::DetachChildren() {
  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::DetachChildren(): node of type '" << type_name()
        << "' cannot have children.";
    return false;
  }
  for (auto& child : children_) {
    child->parent_relation_ = ParentRelation::kNone;
    child->parent_ = nullptr;
    child->InvalidateGlobalTransform();
  }
  children_.clear();
  return true;
}

bool Node::SetTagValue(uint32_t tag_value) {
  tag_value_ = tag_value;
  return true;
}

bool Node::SetTransform(const escher::Transform& transform) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetTransform(): node of type " << type_name()
        << " cannot have transform set.";
    return false;
  }
  transform_ = transform;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetTranslation(const escher::vec3& translation) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetTranslation(): node of type " << type_name()
        << " cannot have translation set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kTranslation);

  transform_.translation = translation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetTranslation(Vector3VariablePtr translation_variable) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetTranslation(): node of type " << type_name()
        << " cannot have translation set.";
    return false;
  }

  bound_variables_[NodeProperty::kTranslation] =
      std::make_unique<Vector3VariableBinding>(translation_variable,
                                               [this](escher::vec3 value) {
                                                 transform_.translation = value;
                                                 InvalidateGlobalTransform();
                                               });
  return true;
}

bool Node::SetScale(const escher::vec3& scale) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::SetScale(): node of type "
                              << type_name() << " cannot have scale set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kScale);
  transform_.scale = scale;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetScale(Vector3VariablePtr scale_variable) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::SetScale(): node of type "
                              << type_name() << " cannot have scale set.";
    return false;
  }
  bound_variables_[NodeProperty::kScale] =
      std::make_unique<Vector3VariableBinding>(scale_variable,
                                               [this](escher::vec3 value) {
                                                 transform_.scale = value;
                                                 InvalidateGlobalTransform();
                                               });
  return true;
}

bool Node::SetRotation(const escher::quat& rotation) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetRotation(): node of type " << type_name()
        << " cannot have rotation set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kRotation);
  transform_.rotation = rotation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetRotation(QuaternionVariablePtr rotation_variable) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetRotation(): node of type " << type_name()
        << " cannot have rotation set.";
    return false;
  }
  bound_variables_[NodeProperty::kRotation] =
      std::make_unique<QuaternionVariableBinding>(rotation_variable,
                                                  [this](escher::quat value) {
                                                    transform_.rotation = value;
                                                    InvalidateGlobalTransform();
                                                  });
  return true;
}

bool Node::SetAnchor(const escher::vec3& anchor) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::SetAnchor(): node of type "
                              << type_name() << " cannot have anchor set.";
    return false;
  }
  bound_variables_.erase(NodeProperty::kAnchor);
  transform_.anchor = anchor;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetAnchor(Vector3VariablePtr anchor_variable) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scenic::gfx::Node::SetAnchor(): node of type "
                              << type_name() << " cannot have anchor set.";
    return false;
  }
  bound_variables_[NodeProperty::kAnchor] =
      std::make_unique<Vector3VariableBinding>(anchor_variable,
                                               [this](escher::vec3 value) {
                                                 transform_.anchor = value;
                                                 InvalidateGlobalTransform();
                                               });
  return true;
}

bool Node::SetClipToSelf(bool clip_to_self) {
  if (!(type_flags() & kHasClip)) {
    error_reporter()->ERROR()
        << "scenic::gfx::Node::SetClipToSelf(): node of type " << type_name()
        << " cannot have clip params set.";
    return false;
  }
  clip_to_self_ = clip_to_self;
  return true;
}

bool Node::SetHitTestBehavior(
    ::fuchsia::ui::gfx::HitTestBehavior hit_test_behavior) {
  hit_test_behavior_ = hit_test_behavior;
  return true;
}

void Node::InvalidateGlobalTransform() {
  if (!global_transform_dirty_) {
    global_transform_dirty_ = true;
    ForEachDirectDescendantFrontToBack(
        *this, [](Node* node) { node->InvalidateGlobalTransform(); });
  }
}

void Node::ComputeGlobalTransform() const {
  if (parent_) {
    global_transform_ =
        parent_->GetGlobalTransform() * static_cast<escher::mat4>(transform_);
  } else {
    global_transform_ = static_cast<escher::mat4>(transform_);
  }
}

void Node::AddImport(Import* import) {
  Resource::AddImport(import);

  auto delegate = static_cast<Node*>(import->delegate());
  FXL_DCHECK(delegate->parent_relation_ == ParentRelation::kNone);
  delegate->parent_ = this;
  delegate->parent_relation_ = ParentRelation::kImportDelegate;

  delegate->InvalidateGlobalTransform();
}

void Node::RemoveImport(Import* import) {
  Resource::RemoveImport(import);

  auto delegate = static_cast<Node*>(import->delegate());
  FXL_DCHECK(delegate->parent_relation_ == ParentRelation::kImportDelegate);
  delegate->parent_relation_ = ParentRelation::kNone;
  delegate->parent_ = nullptr;

  delegate->InvalidateGlobalTransform();
}

bool Node::GetIntersection(const escher::ray4& ray, float* out_distance) const {
  return false;
}

}  // namespace gfx
}  // namespace scenic
