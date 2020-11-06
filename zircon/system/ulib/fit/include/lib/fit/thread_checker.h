// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A class for checking that the current thread is/isn't the same as an initial
// thread.

#ifndef LIB_FIT_THREAD_CHECKER_H_
#define LIB_FIT_THREAD_CHECKER_H_

#include <assert.h>

#include <thread>

#include "thread_safety.h"

namespace fit {

// A simple class that records the identity of the thread that it was created
// on, and at later points can tell if the current thread is the same as its
// creation thread. This class is thread-safe.
//
// In addition to providing an explicit check of the current thread,
// |thread_checker| complies with BasicLockable, checking the current thread
// when |lock| is called. This allows static thread safety analysis to be used
// to ensure that resources are accessed in a context that is checked (at debug
// runtime) to ensure that it's running on the correct thread:
//
// class MyClass {
//  public:
//    void Foo() {
//      std::lock_guard<fit::thread_checker> locker(thread_checker_);
//      resource_ = 0;
//    }
//  private:
//   fit::thread_checker thread_checker_;
//   int resource_ FIT_GUARDED_BY(thread_checker_);
// }
//
// Note: |lock| checks the thread in debug builds only.
//
class FIT_CAPABILITY("mutex") thread_checker final {
 public:
  // Default constructor. Constructs a thread checker bound to the currently
  // running thread.
  thread_checker() : self_(std::this_thread::get_id()) {}
  ~thread_checker() = default;

  // Returns true if the current thread is the thread this object was created
  // on and false otherwise.
  bool is_thread_valid() const { return std::this_thread::get_id() == self_; }

  // Implementation of the BaseLockable requirement
  void lock() FIT_ACQUIRE() { assert(is_thread_valid()); }

  void unlock() FIT_RELEASE() {}

 private:
  const std::thread::id self_;
};

#ifndef NDEBUG
#define FIT_DECLARE_THREAD_CHECKER(c) fit::thread_checker c
#define FIT_DCHECK_IS_THREAD_VALID(c) assert((c).is_thread_valid())
#else
#define FIT_DECLARE_THREAD_CHECKER(c)
#define FIT_DCHECK_IS_THREAD_VALID(c) ((void)0)
#endif

}  // namespace fit

#endif  // LIB_FIT_THREAD_CHECKER_H_
