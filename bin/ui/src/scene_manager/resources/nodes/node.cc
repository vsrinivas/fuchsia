// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "apps/mozart/src/scene_manager/resources/nodes/node.h"

#include "apps/mozart/src/scene_manager/resources/import.h"
#include "apps/mozart/src/scene_manager/resources/nodes/traversal.h"
#include "apps/mozart/src/scene_manager/util/error_reporter.h"
#include "lib/escher/escher/geometry/types.h"

namespace mozart {
namespace scene {

namespace {

constexpr ResourceTypeFlags kHasChildren =
    ResourceType::kEntityNode | ResourceType::kScene;
constexpr ResourceTypeFlags kHasParts =
    ResourceType::kEntityNode | ResourceType::kClipNode;
constexpr ResourceTypeFlags kHasTransform =
    ResourceType::kClipNode | ResourceType::kEntityNode | ResourceType::kScene |
    ResourceType::kShapeNode;
constexpr ResourceTypeFlags kHasClip = ResourceType::kEntityNode;

}  // anonymous namespace

const ResourceTypeInfo Node::kTypeInfo = {ResourceType::kNode, "Node"};

Node::Node(Session* session,
           ResourceId node_id,
           const ResourceTypeInfo& type_info)
    : Resource(session, node_id, type_info) {
  FTL_DCHECK(type_info.IsKindOf(Node::kTypeInfo));
}

Node::~Node() {
  ForEachDirectDescendantFrontToBack(*this, [](Node* node) {
    FTL_DCHECK(node->parent_relation_ != ParentRelation::kNone);
    node->parent_relation_ = ParentRelation::kNone;
    node->parent_ = nullptr;
  });
}

bool Node::AddChild(NodePtr child_node) {
  // TODO(MZ-130): Some node types (e.g. Scenes) cannot be reparented. We must
  // add verification to reject such operations.

  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR() << "scene::Node::AddChild(): node of type '"
                              << type_name() << "' cannot have children.";
    return false;
  }

  if (child_node->parent_relation_ == ParentRelation::kChild &&
      child_node->parent_ == this) {
    return true;  // no change
  }
  Detach(child_node);

  // Add child to its new parent (i.e. us).
  child_node->parent_relation_ = ParentRelation::kChild;
  child_node->parent_ = this;
  child_node->InvalidateGlobalTransform();
  children_.push_back(std::move(child_node));
  return true;
}

bool Node::AddPart(NodePtr part_node) {
  if (!(type_flags() & kHasParts)) {
    error_reporter()->ERROR() << "scene::Node::AddPart(): node of type "
                              << type_name() << " cannot have parts.";
    return false;
  }

  if (part_node->parent_relation_ == ParentRelation::kPart &&
      part_node->parent_ == this) {
    return true;  // no change
  }
  Detach(part_node);

  // Add part to its new parent (i.e. us).
  part_node->parent_relation_ = ParentRelation::kPart;
  part_node->parent_ = this;
  part_node->InvalidateGlobalTransform();
  parts_.push_back(std::move(part_node));
  return true;
}

bool Node::Detach(const NodePtr& node_to_detach_from_parent) {
  FTL_DCHECK(node_to_detach_from_parent);

  if (node_to_detach_from_parent->type_flags() & ResourceType::kScene) {
    node_to_detach_from_parent->error_reporter()->ERROR()
        << "A Scene cannot be detached.";
    return false;
  }

  if (auto parent = node_to_detach_from_parent->parent_) {
    switch (node_to_detach_from_parent->parent_relation_) {
      case ParentRelation::kChild: {
        auto it = std::find(parent->children_.begin(), parent->children_.end(),
                            node_to_detach_from_parent);
        FTL_DCHECK(it != parent->children_.end());
        parent->children_.erase(it);
        break;
      }
      case ParentRelation::kPart: {
        auto it = std::find(parent->parts_.begin(), parent->parts_.end(),
                            node_to_detach_from_parent);
        FTL_DCHECK(it != parent->parts_.end());
        parent->parts_.erase(it);
        break;
      }
      case ParentRelation::kImportDelegate:
        node_to_detach_from_parent->error_reporter()->ERROR()
            << "An imported node cannot be detached.";
        return false;
      case ParentRelation::kNone:
        FTL_NOTREACHED();
        break;
    }

    node_to_detach_from_parent->parent_relation_ = ParentRelation::kNone;
    node_to_detach_from_parent->parent_ = nullptr;
    node_to_detach_from_parent->InvalidateGlobalTransform();
  }
  return true;
}

bool Node::DetachChildren() {
  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR() << "scene::Node::DetachChildren(): node of type '"
                              << type_name() << "' cannot have children.";
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
    error_reporter()->ERROR() << "scene::Node::SetTransform(): node of type "
                              << type_name() << " cannot have transform set.";
    return false;
  }
  transform_ = transform;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetTranslation(const escher::vec3& translation) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scene::Node::SetTranslation(): node of type "
                              << type_name() << " cannot have translation set.";
    return false;
  }
  transform_.translation = translation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetScale(const escher::vec3& scale) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scene::Node::SetScale(): node of type "
                              << type_name() << " cannot have scale set.";
    return false;
  }
  transform_.scale = scale;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetRotation(const escher::quat& rotation) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scene::Node::SetRotation(): node of type "
                              << type_name() << " cannot have rotation set.";
    return false;
  }
  transform_.rotation = rotation;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetAnchor(const escher::vec3& anchor) {
  if (!(type_flags() & kHasTransform)) {
    error_reporter()->ERROR() << "scene::Node::SetAnchor(): node of type "
                              << type_name() << " cannot have anchor set.";
    return false;
  }
  transform_.anchor = anchor;
  InvalidateGlobalTransform();
  return true;
}

bool Node::SetClipToSelf(bool clip_to_self) {
  if (!(type_flags() & kHasClip)) {
    error_reporter()->ERROR() << "scene::Node::SetClipToSelf(): node of type "
                              << type_name() << " cannot have clip params set.";
    return false;
  }
  clip_to_self_ = clip_to_self;
  return true;
}

bool Node::SetHitTestBehavior(mozart2::HitTestBehavior hit_test_behavior) {
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
  FTL_DCHECK(delegate->parent_relation_ == ParentRelation::kNone);
  delegate->parent_ = this;
  delegate->parent_relation_ = ParentRelation::kImportDelegate;

  delegate->InvalidateGlobalTransform();
}

void Node::RemoveImport(Import* import) {
  Resource::RemoveImport(import);

  auto delegate = static_cast<Node*>(import->delegate());
  FTL_DCHECK(delegate->parent_relation_ == ParentRelation::kImportDelegate);
  delegate->parent_relation_ = ParentRelation::kNone;
  delegate->parent_ = nullptr;

  delegate->InvalidateGlobalTransform();
}

bool Node::GetIntersection(const escher::ray4& ray, float* out_distance) const {
  return false;
}

}  // namespace scene
}  // namespace mozart
