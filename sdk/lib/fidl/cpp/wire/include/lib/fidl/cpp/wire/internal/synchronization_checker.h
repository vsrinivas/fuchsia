// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_SYNCHRONIZATION_CHECKER_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_SYNCHRONIZATION_CHECKER_H_

#include <lib/async/cpp/sequence_checker.h>
#include <zircon/compiler.h>

#include <optional>

namespace fidl::internal {

enum class __attribute__((enum_extensibility(closed))) ThreadingPolicy {
  // The user may create the |AsyncBinding| and initiate teardown from any
  // thread.
  kCreateAndTeardownFromAnyThread = 0,

  // The user may only create the |AsyncBinding| and initiate teardown from the
  // single thread that backs the async dispatcher. Implied requirement: there
  // can only be one thread backing the dispatcher.
  kCreateAndTeardownFromDispatcherThread,
};

// |SynchronizationChecker| accepts a policy where it may optionally check
// for synchronized access. It is always used within an |AsyncBinding|.
// Client/server types that are designed for synchronized environments will
// verify such invariants at run-time.
//
// When access to the binding is synchronized, the same binding will never be
// used or destroyed in parallel. As such, it protects against data races and
// use-after-free when calling into user code.
//
// This class uses |async::synchronization_checker| internally. Refer to the
// documentation on that class for the definition of synchronized access.
class __TA_CAPABILITY("mutex") SynchronizationChecker final {
 public:
  explicit SynchronizationChecker(async_dispatcher_t* dispatcher, ThreadingPolicy policy)
      : policy_(policy), checker_(MakeChecker(dispatcher, policy)) {}

  // Checks for exclusive access.
  void check() const __TA_ACQUIRE() {
    if (checker_.has_value()) {
      checker_->lock();
    }
  }

  // Assumes exclusive access without checking. This should only be used
  // when mutual exclusion is guaranteed via other means (e.g. external
  // synchronization between two threads).
  void assume_exclusive() const __TA_ACQUIRE() {}

  ThreadingPolicy policy() const { return policy_; }

 private:
  static std::optional<async::synchronization_checker> MakeChecker(async_dispatcher_t* dispatcher,
                                                                   ThreadingPolicy policy) {
    switch (policy) {
      case ThreadingPolicy::kCreateAndTeardownFromDispatcherThread:
        return std::make_optional(async::synchronization_checker{
            dispatcher, "The selected FIDL bindings is thread unsafe."});
      case ThreadingPolicy::kCreateAndTeardownFromAnyThread:
        return std::nullopt;
    }
  }

  const ThreadingPolicy policy_;
  const std::optional<async::synchronization_checker> checker_;
};

// A synchronization checker that does nothing.
class __TA_CAPABILITY("mutex") NoopSynchronizationChecker final {
 public:
  void check() const __TA_ACQUIRE() {}

  void assume_exclusive() const __TA_ACQUIRE() {}
};

// |DebugOnlySynchronizationChecker| only checks synchronization in debug builds.
// This ensures zero overhead in release builds.
class __TA_CAPABILITY("mutex") DebugOnlySynchronizationChecker {
 public:
#ifndef NDEBUG
  using CheckerType = SynchronizationChecker;

  explicit DebugOnlySynchronizationChecker(async_dispatcher_t* dispatcher, ThreadingPolicy policy)
      : checker_(dispatcher, policy) {}
#else
  using CheckerType = NoopSynchronizationChecker;

  explicit DebugOnlySynchronizationChecker(async_dispatcher_t*, ThreadingPolicy) {}
#endif

  // Checks for exclusive access.
  void check() const __TA_ACQUIRE() { checker_.check(); }

  // Assumes exclusive access without checking. This should only be used
  // when mutual exclusion is guaranteed via other means (e.g. external
  // synchronization between two threads).
  void assume_exclusive() const __TA_ACQUIRE() { checker_.assume_exclusive(); }

 private:
  [[no_unique_address]] CheckerType checker_;
};

// A scoped capability that performs synchronization checking when entering the
// guarded scope.
class __TA_SCOPED_CAPABILITY ScopedThreadGuard final {
 public:
  explicit ScopedThreadGuard(const DebugOnlySynchronizationChecker& checker) __TA_ACQUIRE(checker)
      : checker_(checker) {
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
  const DebugOnlySynchronizationChecker& checker_;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_SYNCHRONIZATION_CHECKER_H_
