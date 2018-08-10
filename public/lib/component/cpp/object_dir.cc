// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "object_dir.h"

namespace component {

ObjectDir::ObjectDir() {}

ObjectDir::ObjectDir(fbl::RefPtr<Object> object) : object_(std::move(object)) {}

ObjectDir ObjectDir::find(ObjectPath path, bool initialize) {
  fbl::RefPtr<Object> current = object_;
  for (const char* p : path) {
    auto next = current->GetChild(p);
    if (!next) {
      if (!initialize) {
        return ObjectDir();
      }
      next = fbl::MakeRefCounted<Object>(p);
      current->SetChild(next);
    }
    current = std::move(next);
  }
  return ObjectDir(current);
}

void ObjectDir::inner_set_prop(ObjectPath path, std::string name,
                               Property property) {
  auto dir = find(path);
  dir.object()->SetProperty(name, std::move(property));
}

void ObjectDir::set_metric(ObjectPath path, std::string name, Metric metric) {
  find(path).object()->SetMetric(name, std::move(metric));
}

void ObjectDir::set_child(ObjectPath path, fbl::RefPtr<Object> obj) {
  find(path).object()->SetChild(obj);
}

void ObjectDir::set_children_callback(ObjectPath path,
                                      Object::ChildrenCallback callback) {
  find(path).object()->SetChildrenCallback(std::move(callback));
}

}  // namespace component
