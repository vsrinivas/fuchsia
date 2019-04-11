// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/hierarchy.h"

#include <assert.h>

namespace inspect {

namespace hierarchy {
Node::Node(std::string name) : name_(std::move(name)) {}

Node::Node(std::string name, std::vector<Property> properties,
           std::vector<Metric> metrics)
    : name_(std::move(name)),
      properties_(std::move(properties)),
      metrics_(std::move(metrics)) {}

void Node::Sort() {
  std::sort(
      properties_.begin(), properties_.end(),
      [](const Property& a, const Property& b) { return a.name() < b.name(); });
  std::sort(
      metrics_.begin(), metrics_.end(),
      [](const Metric& a, const Metric& b) { return a.name() < b.name(); });
}

}  // namespace hierarchy

ObjectHierarchy::ObjectHierarchy(hierarchy::Node node,
                                 std::vector<ObjectHierarchy> children)
    : node_(std::move(node)), children_(std::move(children)) {}

const ObjectHierarchy* ObjectHierarchy::GetByPath(
    std::vector<std::string> path) const {
  const ObjectHierarchy* current = this;
  auto path_it = path.begin();

  while (current && path_it != path.end()) {
    const ObjectHierarchy* next = nullptr;
    for (const auto& obj : current->children_) {
      if (obj.node().name() == *path_it) {
        next = &obj;
        break;
      }
    }
    current = next;
    ++path_it;
  }
  return current;
}

void ObjectHierarchy::Sort() {
  node_.Sort();
  std::sort(
      children_.begin(), children_.end(),
      [](const inspect::ObjectHierarchy& a, const inspect::ObjectHierarchy& b) {
        return a.node().name() < b.node().name();
      });
}

}  // namespace inspect
