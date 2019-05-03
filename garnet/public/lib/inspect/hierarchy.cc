// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/hierarchy.h"

#include <assert.h>

#include <algorithm>
#include <cctype>
#include <stack>
#include <vector>

namespace inspect {

namespace {
// Helper to sort an array of T by the value of T::name().
// If names are numeric unsigned integers, this function will sort numerically
// rather than lexicographically. Note that this will not handle negative or
// decimal numbers.
template <typename T>
void SortByName(std::vector<T>* values) {
  if (std::all_of(values->begin(), values->end(), [](const T& value) {
        for (char c : value.name()) {
          if (!std::isdigit(c)) {
            return false;
          }
        }
        return value.name().size() > 0;
      })) {
    std::sort(values->begin(), values->end(), [](const T& a, const T& b) {
      uint64_t a_val = atoll(a.name().c_str());
      uint64_t b_val = atoll(b.name().c_str());
      return a_val < b_val;
    });
  } else {
    std::sort(values->begin(), values->end(),
              [](const T& a, const T& b) { return a.name() < b.name(); });
  }
}
}  // namespace

namespace hierarchy {
Node::Node(std::string name) : name_(std::move(name)) {}

Node::Node(std::string name, std::vector<Property> properties,
           std::vector<Metric> metrics)
    : name_(std::move(name)),
      properties_(std::move(properties)),
      metrics_(std::move(metrics)) {}

void Node::Sort() {
  SortByName(&properties_);
  SortByName(&metrics_);
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
  SortByName(&children_);
  for (auto& child : children_) {
    child.Sort();
  }
}

}  // namespace inspect
