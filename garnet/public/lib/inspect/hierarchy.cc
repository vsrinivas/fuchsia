// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/hierarchy.h"

namespace inspect {

const ObjectHierarchy* ObjectHierarchy::GetByPath(
    std::vector<std::string> path) const {
  const ObjectHierarchy* current = this;
  auto path_it = path.begin();

  while (current && path_it != path.end()) {
    const ObjectHierarchy* next = nullptr;
    for (const auto& obj : current->children_) {
      if (obj.object().name == *path_it) {
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
  std::sort(object_.metrics->begin(), object_.metrics->end(),
            [](const fuchsia::inspect::Metric& a,
               const fuchsia::inspect::Metric& b) { return a.key < b.key; });

  std::sort(object_.properties->begin(), object_.properties->end(),
            [](const fuchsia::inspect::Property& a,
               const fuchsia::inspect::Property& b) { return a.key < b.key; });

  std::sort(
      children_.begin(), children_.end(),
      [](const inspect::ObjectHierarchy& a, const inspect::ObjectHierarchy& b) {
        return a.object().name < b.object().name;
      });
}

}  // namespace inspect
