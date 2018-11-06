// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <string>

#include <fbl/atomic.h>
#include <fbl/ref_ptr.h>
#include <lib/fxl/strings/string_printf.h>

#include "exposed_object.h"

namespace component {

ExposedObject::ExposedObject(const std::string& name)
    : object_dir_(fbl::MakeRefCounted<Object>(name.c_str())) {}

ExposedObject::~ExposedObject() { remove_from_parent(); }

void ExposedObject::set_parent(ObjectDir parent) { move_parents(&parent); }

void ExposedObject::set_parent(const ObjectDir* parent) {
  move_parents(parent);
}

void ExposedObject::add_child(ExposedObject& child) { add_child(&child); }

void ExposedObject::add_child(ExposedObject* child) {
  child->set_parent(&object_dir_);
}

void ExposedObject::remove_from_parent() { move_parents(nullptr); }

void ExposedObject::move_parents(const ObjectDir* new_parent) {
  if (parent_) {
    parent_.object()->TakeChild(object_dir_.object()->name());
  }
  if (new_parent != nullptr) {
    new_parent->object()->SetChild(object_dir_.object());
    parent_ = *new_parent;
  }
}

std::string ExposedObject::UniqueName(const std::string& prefix) {
  static fbl::atomic_uint_fast64_t next_id;
  std::string out = prefix;
  auto value = next_id.fetch_add(1);
  fxl::StringAppendf(&out, "0x%lx", value);
  return out;
}

}  // namespace component
