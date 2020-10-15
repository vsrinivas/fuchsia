// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_
#define LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_

#include <stdint.h>
#include <threads.h>
#include <zircon/assert.h>

#include <atomic>

namespace fidl {
namespace internal {
class StubController;

// A weak reference to a |StubController|.
//
// Used to link a |PendingResponse| object with a |StubController|. When the
// |StubController| is destroyed (or unbound from the underling channel), the
// weak reference is invalidated, preventing outstanding |PendingResponse|
// objects from referencing the |StubController|.
class WeakStubController final {
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
  // Cannot be called concurrently with AddRef or Release.
  void Invalidate();

  // The |StubController| to which this weak reference refers.
  //
  // After the weak reference has been invalidated, this method returns nullptr.
  // Cannot be called concurrently with AddRef or Release.
  StubController* controller() const;

 private:
  ~WeakStubController();

  // Starts at one. This is atomic so that PendingResponse objects can be deleted
  // on any thread. See fxrev.dev/437828 for a longer discussion.
  std::atomic<uint32_t> ref_count_;
  StubController* controller_;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
  // thrd_current() needs to match the thread on which this instance was
  // created, so that non-atomic ref_count_ updates work as intended.
  bool IsCurrentThreadOk() const;
#endif
  // If the WeakStubController constructor is release build, this field will be initialized to
  // thrd_t{} by the constructor and stay that value until destruction. If debug build, this field
  // remembers the construction thread. In a debug build, non-thread-safe methods will ZX_PANIC()
  // if run on a thread other than the construction thread AND thread != thrd_t{}. By checking vs.
  // thrd_t{}, a WeakStubController constructed by release code, but passed to a debug method, will
  // avoid asserting.
  thrd_t thread_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_WEAK_STUB_CONTROLLER_H_
