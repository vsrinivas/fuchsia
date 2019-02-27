// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_HIERARCHY_H_
#define LIB_INSPECT_HIERARCHY_H_

#include <fuchsia/inspect/cpp/fidl.h>

#include <vector>

namespace inspect {

// Represents a hierarchy of objects rooted under one particular object.
// This class includes constructors that handle reading the hierarchy from
// various sources.
class ObjectHierarchy {
 public:
  ObjectHierarchy() = default;

  // Directly construct an object hierarchy consisting of an object and a list
  // of children.
  ObjectHierarchy(fuchsia::inspect::Object object,
                  std::vector<ObjectHierarchy> children);

  // Allow moving, disallow copying.
  ObjectHierarchy(ObjectHierarchy&&) = default;
  ObjectHierarchy(const ObjectHierarchy&) = delete;
  ObjectHierarchy& operator=(ObjectHierarchy&&) = default;
  ObjectHierarchy& operator=(const ObjectHierarchy&) = delete;

  // Gets the FIDL representation of the Object.
  const fuchsia::inspect::Object& object() const { return object_; };
  fuchsia::inspect::Object& object() { return object_; };

  // Gets the children of this object in the hierarchy.
  const std::vector<ObjectHierarchy>& children() const { return children_; };
  std::vector<ObjectHierarchy>& children() { return children_; };

  // Gets a child in this ObjectHierarchy by path.
  // Returns NULL if the requested child could not be found.
  const ObjectHierarchy* GetByPath(std::vector<std::string> path) const;

 private:
  fuchsia::inspect::Object object_;
  std::vector<ObjectHierarchy> children_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_HIERARCHY_H_
