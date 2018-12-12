// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <optional>
#include <string>
#include <sstream>

#include <fbl/ref_ptr.h>

#include "exposed_object.h"

namespace component {

ExposedObject::ExposedObject(const std::string& name)
    : object_dir_(std::make_shared<Object>(name.c_str())) {}

ExposedObject::~ExposedObject() { remove_from_parent(); }

void ExposedObject::set_parent(const ObjectDir& parent) {
  move_parents(parent);
}

void ExposedObject::add_child(ExposedObject* child) {
  child->set_parent(object_dir_);
}

void ExposedObject::remove_from_parent() {
  if (parent_) {
    parent_.object()->TakeChild(object_dir_.object()->name());
    parent_ = ObjectDir();
  }
}

void ExposedObject::move_parents(const ObjectDir& new_parent) {
  remove_from_parent();
  if (new_parent) {
    new_parent.object()->SetChild(object_dir_.object());
  }
  parent_ = new_parent;
}

std::string ExposedObject::UniqueName(const std::string& prefix) {
  static std::atomic_uint_fast64_t next_id;
  std::ostringstream out;
  auto value = next_id.fetch_add(1);
  out << prefix << "0x" << std::hex << value;
  return out.str();
}

}  // namespace component
