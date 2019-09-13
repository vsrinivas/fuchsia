// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REFCOUNT_BLOCKING_REFCOUNT_H_
#define REFCOUNT_BLOCKING_REFCOUNT_H_

#include <lib/sync/condition.h>
#include <lib/sync/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace refcount {

// A BlockingRefCount provides a counter which can be incremented and
// decremented, with an additional operation allowing threads to wait
// for the count to become zero.
//
// This can be useful in scenarios where an object is waiting for
// in-flight callbacks to complete before cleaning up resources,
// for example:
//
//   class MyClass {
//     void PerformAsyncOperation() {
//       // Increment the counter when we start the work, and decrement
//       // it when finished.
//       in_flight_ops_.Inc();
//       DoWork(..., /*completion_callback=*/[this](){
//         // ...
//         in_flight_ops_.Dec();
//       });
//     }
//
//     // Wait for all in-flight operations to terminate before
//     // destructing.
//     ~MyClass() {
//       in_flight_ops_.WaitForZero();
//     }
//
//     BlockingRefCount in_flight_ops_;
//   }
//
// BlockingRefCount must not be destructed while threads are waiting
// on it.
//
// Thread safe.
class BlockingRefCount {
 public:
  // Create a new BlockingRefCount with initial reference count of 0.
  BlockingRefCount();

  // Create a new BlockingRefCount with the given initial reference count.
  explicit BlockingRefCount(int32_t initial_count);

  // Increment the reference count.
  void Inc();

  // Decrement the reference count, potentially waking up threads waiting
  // for the count to reach zero.
  //
  // Callers must ensure that calling this would not result in the counter
  // dropping below zero.
  void Dec();

  // Wait for the counter to become zero.
  //
  // If the counter only briefly becomes zero, waiting threads may not
  // see the zero and fail to wake up. If the counter hits zero and
  // remains, however, threads are guaranteed to wake up.
  void WaitForZero() const;

 private:
  // Number of references to this object.
  mutable sync_mutex_t lock_;
  mutable sync_condition_t condition_
      __TA_GUARDED(lock_);             // Threads waiting on the count to reach zero.
  int32_t count_ __TA_GUARDED(lock_);  // Number of references.
};

}  // namespace refcount

#endif  // REFCOUNT_BLOCKING_REFCOUNT_H_
