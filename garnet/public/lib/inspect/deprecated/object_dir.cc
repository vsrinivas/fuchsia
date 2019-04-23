// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/deprecated/object_dir.h>

namespace component {

ObjectDir::ObjectDir() {}

ObjectDir::ObjectDir(std::shared_ptr<Object> object)
    : object_(std::move(object)) {}

ObjectDir ObjectDir::find(ObjectPath path, bool initialize) const {
  if (!object_) {
    return ObjectDir();
  }
  std::shared_ptr<Object> current = object_;
  for (const char* p : path) {
    auto next = current->GetChild(p);
    if (!next) {
      if (!initialize) {
        return ObjectDir();
      }
      next = Object::Make(p);
      current->SetChild(next);
    }
    current = std::move(next);
  }
  return ObjectDir(current);
}

bool ObjectDir::inner_set_prop(ObjectPath path, std::string name,
                               Property property) const {
  auto dir = find(path);
  return dir ? dir.object()->SetProperty(name, std::move(property)) : false;
}

bool ObjectDir::set_metric(ObjectPath path, std::string name,
                           Metric metric) const {
  return object_ ? find(path).object()->SetMetric(name, std::move(metric))
                 : false;
}

void ObjectDir::set_child(ObjectPath path, std::shared_ptr<Object> obj) const {
  if (object_) {
    find(path).object()->SetChild(obj);
  }
}

void ObjectDir::set_children_callback(ObjectPath path,
                                      Object::ChildrenCallback callback) const {
  if (object_) {
    find(path).object()->SetChildrenCallback(std::move(callback));
  }
}

}  // namespace component
