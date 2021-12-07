// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_
#define LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_

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

// A thread checker that actually checks it is always used from the same thread.
class __TA_CAPABILITY("mutex") WorkingThreadChecker final {
 public:
  explicit WorkingThreadChecker(ThreadingPolicy policy) : policy_(policy) {}

  // Checks for exclusive access by checking that the current thread is the
  // same as the constructing thread.
  void check() const __TA_ACQUIRE() {
    if (policy_ == ThreadingPolicy::kCreateAndTeardownFromDispatcherThread) {
      checker_.lock();
    }
  }

  // Assumes exclusive access without checking threads. This should only be used
  // when mutual exclusion is guaranteed via other means (e.g. external
  // synchronization between two threads).
  void assume_exclusive() const __TA_ACQUIRE() {}

 private:
  const ThreadingPolicy policy_;
  const fit::thread_checker checker_;
};

// A thread checker that does nothing.
class __TA_CAPABILITY("mutex") NoopThreadChecker final {
 public:
  explicit NoopThreadChecker(ThreadingPolicy policy) {}

  void check() const __TA_ACQUIRE() {}

  void assume_exclusive() const __TA_ACQUIRE() {}
};

// |ThreadChecker| accepts a threading policy that specifies how it should check
// the current invoker thread. It is always used within an |AsyncBinding|. The
// intended usage is that client/server types that are designed to live on a
// fixed thread would configure the thread checker in |AsyncBinding| to verify
// such invariants at run-time.
//
// When |ThreadChecker::check| is called, it asserts that the identity of the
// calling thread is the same as the thread which initially created the thread
// checker.
//
// |ThreadChecker| only check threads in debug builds.
using ThreadChecker =
#ifndef NDEBUG
    WorkingThreadChecker;
#else
    NoopThreadChecker;
#endif

// A scoped capability that performs thread checking when entering the guarded
// scope.
class __TA_SCOPED_CAPABILITY ScopedThreadGuard final {
 public:
  explicit ScopedThreadGuard(const ThreadChecker& thread_checker) __TA_ACQUIRE(thread_checker)
      : checker_(thread_checker) {
    checker_.check();
  }

  // Clang thread safety annotation doesn't work with a defaulted destructor.
  // NOLINTNEXTLINE
  ~ScopedThreadGuard() __TA_RELEASE() {
    // |checker_| may have already destructed when we exit the scope. That is
    // a-okay since there is nothing to do when releasing this capability
    // anyways. The |__TA_RELEASE| is used to appease the the thread safety
    // sanitizers.
  }

 private:
  const ThreadChecker& checker_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_THREAD_CHECKER_H_
