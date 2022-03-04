// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_
#define LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_

#include <lib/fidl/llcpp/internal/any.h>
#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fit/thread_checker.h>
#include <zircon/compiler.h>

namespace fidl {
namespace internal {

enum class __attribute__((enum_extensibility(closed))) ThreadingPolicy {
  // The user may create the |AsyncBinding| and initiate teardown from any
  // thread.
  kCreateAndTeardownFromAnyThread = 0,

  // The user may only create the |AsyncBinding| and initiate teardown from the
  // single thread that backs the async dispatcher. Implied requirement: there
  // can only be one thread backing the dispatcher.
  kCreateAndTeardownFromDispatcherThread,
};

// An interface for checking thread identity.
//
// Note: for the remainder of the class documentation, we mean "thread" as an
// abstract concept representing a sequential ordering of execution, that may be
// different across transports. For example, when using clients and servers over
// a |async_dispatcher_t*|, we should check that the physical thread IDs match.
// On the other hand, other runtimes whose dispatchers have a concept of virtual
// threads should check for virtual thread identities.
//
// |ThreadChecker| accepts a threading policy that specifies how it should check
// the current invoker thread. It is always used within an |AsyncBinding|. The
// intended usage is that client/server types that are designed to live on a
// fixed thread would configure the thread checker in |AsyncBinding| to verify
// such invariants at run-time.
//
// When |ThreadChecker::check| is called, it asserts that the identity of the
// calling thread is the same as the thread which initially created the thread
// checker.
class __TA_CAPABILITY("mutex") ThreadChecker {
 public:
  explicit ThreadChecker(ThreadingPolicy policy) : policy_(policy) {}

  virtual ~ThreadChecker() = default;

  // Checks for exclusive access by checking that the current thread is the
  // same as the constructing thread.
  virtual void check() const __TA_ACQUIRE() = 0;

  // Assumes exclusive access without checking threads. This should only be used
  // when mutual exclusion is guaranteed via other means (e.g. external
  // synchronization between two threads).
  void assume_exclusive() const __TA_ACQUIRE() {}

  ThreadingPolicy policy() const { return policy_; }

 private:
  const ThreadingPolicy policy_;
};

using AnyThreadChecker = Any<ThreadChecker, 24>;

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_
