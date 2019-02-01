// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_
#define LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_

#include <stdint.h>

namespace fidl {
namespace internal {
class StubController;

// A weak reference to a |StubController|.
//
// Used to link a |PendingResponse| object with a |StubController|. When the
// |StubController| is destroyed (or unbound from the underling channel), the
// weak reference is invalidated, preventing outstanding |PendingResponse|
// objects from referencing the |StubController|.
class WeakStubController {
 public:
  // Creates a weak reference to a |StubController|.
  //
  // The created |WeakStubController| has a reference count of one, which means
  // the creator is responsible for calling |Release| exactly once.
  explicit WeakStubController(StubController* controller);

  // Increment the refernence count for this object.
  //
  // Each call to this method imposes a requirement to eventually call |Release|
  // exactly once.
  void AddRef();

  // Decrements the reference count for this object.
  //
  // When the reference count reaches zero, the object is destroyed.
  void Release();

  // Break the connection between this object and the |StubController|.
  //
  // After calling this method, |controller()| will return nullptr.
  void Invalidate();

  // The |StubController| to which this weak reference refers.
  //
  // After the weak reference has been invalidated, this method returns nullptr.
  StubController* controller() const { return controller_; }

 private:
  ~WeakStubController();

  uint32_t ref_count_;  // starts at one
  StubController* controller_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_
