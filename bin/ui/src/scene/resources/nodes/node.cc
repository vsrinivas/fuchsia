// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/nodes/node.h"

#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/util/error_reporter.h"
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

}  // anonymous namespace

const ResourceTypeInfo Node::kTypeInfo = {ResourceType::kNode, "Node"};

Node::Node(Session* session,
           ResourceId node_id,
           const ResourceTypeInfo& type_info)
    : Resource(session, type_info), resource_id_(node_id) {
  FTL_DCHECK(type_info.IsKindOf(Node::kTypeInfo));
}

Node::~Node() = default;

bool Node::AddChild(NodePtr child_node) {
  // TODO(MZ-130): Some node types (e.g. Scenes) cannot be reparented. We must
  // add verification to reject such operations.

  if (!(type_flags() & kHasChildren)) {
    error_reporter()->ERROR() << "scene::Node::AddChild(): node of type '"
                              << type_name() << "' cannot have children.";
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
  child_node->InvalidateGlobalTransform();

  auto insert_result = children_.insert(std::move(child_node));
  FTL_DCHECK(insert_result.second);

  return true;
}

bool Node::AddPart(NodePtr part_node) {
  if (!(type_flags() & kHasParts)) {
    error_reporter()->ERROR() << "scene::Node::AddPart(): node of type "
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
  part_node->InvalidateGlobalTransform();

  auto insert_result = children_.insert(std::move(part_node));
  FTL_DCHECK(insert_result.second);

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
    auto& container = node_to_detach_from_parent->is_part_ ? parent->parts_
                                                           : parent->children_;
    size_t removed_count = container.erase(node_to_detach_from_parent);
    FTL_DCHECK(removed_count == 1);  // verify parent-child invariant
    node_to_detach_from_parent->parent_ = nullptr;
    node_to_detach_from_parent->InvalidateGlobalTransform();
  }
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

void Node::InvalidateGlobalTransform() {
  if (!global_transform_dirty_) {
    global_transform_dirty_ = true;
    for (auto& node : parts_) {
      node->InvalidateGlobalTransform();
    }
    for (auto& node : children_) {
      node->InvalidateGlobalTransform();
    }
    for (auto& import : imports()) {
      static_cast<Node*>(import->delegate())->InvalidateGlobalTransform();
    }
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

escher::vec2 Node::ConvertPointFromNode(const escher::vec2& point,
                                        const Node& node) const {
  auto inverted = glm::inverse(GetGlobalTransform());
  auto adjusted =
      glm::vec4{point.x, point.y, 0.0f, 1.0f} * node.GetGlobalTransform();
  auto result = adjusted * inverted;
  return {result.x, result.y};
}

bool Node::ContainsPoint(const escher::vec2& point) const {
  bool inside = false;
  ApplyOnDescendants([&inside, &point](const Node& descendant) -> bool {
    if (descendant.ContainsPoint(point)) {
      inside = true;
      // At least one of our descendants has accepted the hit test. We no longer
      // need to traverse to find the node. Return false and stop the iteration.
      return false;
    }
    return true;
  });
  return inside;
}

void Node::AddImport(Import* import) {
  Resource::AddImport(import);

  auto delegate = static_cast<Node*>(import->delegate());
  FTL_DCHECK(!delegate->parent_);
  delegate->parent_ = this;
  delegate->InvalidateGlobalTransform();
}

void Node::RemoveImport(Import* import) {
  Resource::RemoveImport(import);

  auto delegate = static_cast<Node*>(import->delegate());
  FTL_DCHECK(delegate->parent_);
  delegate->parent_ = nullptr;
  delegate->InvalidateGlobalTransform();
}

void Node::ApplyOnDescendants(std::function<bool(const Node&)> applier) const {
  if (!applier) {
    return;
  }

  for (const auto& node : children_) {
    if (!applier(*node)) {
      return;
    }
  }

  for (const auto& node : parts_) {
    if (!applier(*node)) {
      return;
    }
  }
}

}  // namespace scene
}  // namespace mozart
