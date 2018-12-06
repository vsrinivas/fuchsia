// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INSPECT_READER_H_
#define LIB_INSPECT_READER_H_

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fit/promise.h>
#include "lib/inspect/inspect.h"

namespace inspect {

// Vector of child names, as returned by the API.
using ChildNameVector = fidl::VectorPtr<std::string>;

namespace internal {
// Wraps the state of the ObjectReader so that ObjectReader remains copyable.
struct ObjectReaderState {
  // The interface used to communicate with Inspect.
  fuchsia::inspect::InspectPtr inspect_ptr_;
};
}  // namespace internal

// ObjectReader is a reading interface to the Inspect API.
// It takes ownership of an incoming channel and communicates with a remote
// Inspect interface over that channel to implement high-level reading
// operations for inspection.
class ObjectReader {
 public:
  // Construct a new Reader that wraps the given handle.
  explicit ObjectReader(
      fidl::InterfaceHandle<fuchsia::inspect::Inspect> inspect_handle);

  // Allow moving and copying.
  ObjectReader(ObjectReader&&) = default;
  ObjectReader(const ObjectReader&) = default;
  ObjectReader& operator=(ObjectReader&&) = default;
  ObjectReader& operator=(const ObjectReader&) = default;

  ~ObjectReader() = default;

  // Reads and returns the value of this Object.
  // Returns a promise to the inspect object value.
  fit::promise<fuchsia::inspect::Object> Read() const;

  // Lists the children of this Object.
  // Returns a promise to a list of child names.
  fit::promise<ChildNameVector> ListChildren() const;

  // Opens a named child of this object.
  // Returns a promise for a new |ObjectReader| pointing at that child.
  fit::promise<ObjectReader> OpenChild(std::string child_name) const;

  // Opens |ObjectReader|s for all children of this object.
  // Returns a promise to the opened Objects as a vector of |ObjectReader|s.
  // Note: The set of children may change between listing and opening a child,
  // so children visible in one call to |ListChildren| may not be included in
  // the opened list.
  fit::promise<std::vector<ObjectReader>> OpenChildren() const;

  // Take the channel from this reader and return it.
  // This will unbind the interface being used by this reader, and future
  // operations using the reader will fail.
  zx::channel TakeChannel() {
    return state_->inspect_ptr_.Unbind().TakeChannel();
  }

 private:
  // Shared pointer to the state for this reader, allowing ObjectReaders to be
  // copied. ObjectReader needs to be copyable to allow keeping the ObjectReader
  // (including its wrapped connection) alive while promises are evaluated.
  std::shared_ptr<internal::ObjectReaderState> state_;
};

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

  // Construct a new object hierarchy by asynchronously reading objects from the
  // given reader.
  // Will only read |depth| levels past the immediate object, or all levels if
  // |depth| is -1.
  static fit::promise<ObjectHierarchy> Make(ObjectReader reader,
                                            int depth = -1);

  // Construct a new object hierarchy by directly reading objects from the
  // given given inspect::Object.
  // Will only read |depth| levels past the immediate object, or all levels if
  // |depth| is -1.
  static ObjectHierarchy Make(const Object& object_root, int depth = -1);

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
  static ObjectHierarchy Make(std::shared_ptr<component::Object> object_root,
                              int depth);

  fuchsia::inspect::Object object_;
  std::vector<ObjectHierarchy> children_;
};

}  // namespace inspect

#endif  // LIB_INSPECT_READER_H_
