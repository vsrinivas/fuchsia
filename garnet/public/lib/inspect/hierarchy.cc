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

}  // namespace inspect
