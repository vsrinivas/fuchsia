// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_DEBUG_THREAD_CHECKER_H_
#define LIB_FIDL_LLCPP_INTERNAL_DEBUG_THREAD_CHECKER_H_

#include <lib/fidl/llcpp/internal/any.h>
#include <lib/fidl/llcpp/internal/arrow.h>
#include <lib/fidl/llcpp/internal/thread_checker.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fit/thread_checker.h>
#include <zircon/compiler.h>

namespace fidl::internal {

// A thread checker that does nothing.
class __TA_CAPABILITY("mutex") NoopThreadChecker final {
 public:
  void check() const __TA_ACQUIRE() {}

  void assume_exclusive() const __TA_ACQUIRE() {}
};

// |DebugOnlyThreadChecker| only check threads in debug builds.
class __TA_CAPABILITY("mutex") DebugOnlyThreadChecker {
 public:
#ifndef NDEBUG
  using CheckerType = AnyThreadChecker;
  explicit DebugOnlyThreadChecker(AnyThreadChecker checker) : checker_(std::move(checker)) {}
#else
  using CheckerType = Arrow<NoopThreadChecker>;
  explicit DebugOnlyThreadChecker(AnyThreadChecker checker) : checker_() {}
#endif

  DebugOnlyThreadChecker(const TransportVTable* vtable, async_dispatcher_t* dispatcher,
                         ThreadingPolicy policy) {
#ifndef NDEBUG
    vtable->create_thread_checker(dispatcher, policy, checker_);
#endif
  }

  // Checks for exclusive access by checking that the current thread is the
  // same as the constructing thread.
  void check() const __TA_ACQUIRE() { checker_->check(); }

  // Assumes exclusive access without checking threads. This should only be used
  // when mutual exclusion is guaranteed via other means (e.g. external
  // synchronization between two threads).
  void assume_exclusive() const __TA_ACQUIRE() { checker_->assume_exclusive(); }

 private:
  CheckerType checker_;
};

// A scoped capability that performs thread checking when entering the guarded
// scope.
class __TA_SCOPED_CAPABILITY ScopedThreadGuard final {
 public:
  explicit ScopedThreadGuard(const DebugOnlyThreadChecker& thread_checker)
      __TA_ACQUIRE(thread_checker)
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
  const DebugOnlyThreadChecker& checker_;
};

// An implementation of |ThreadChecker| that checks physical threads.
// This is useful in tests and for bindings over regular |async_dispatcher_t|.
class __TA_CAPABILITY("mutex") ZirconThreadChecker final : public ThreadChecker {
 public:
  explicit ZirconThreadChecker(ThreadingPolicy policy) : ThreadChecker(policy) {}

  // Checks for exclusive access by checking that the current thread is the
  // same as the constructing thread.
  void check() const __TA_ACQUIRE() override {
    if (policy() == ThreadingPolicy::kCreateAndTeardownFromDispatcherThread) {
      checker_.lock();
    }
  }

 private:
  const fit::thread_checker checker_;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INTERNAL_DEBUG_THREAD_CHECKER_H_
