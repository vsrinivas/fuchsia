// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A class for checking that the current thread is/isn't the same as an initial
// thread.

#ifndef LIB_FTL_SYNCHRONIZATION_THREAD_CHECKER_H_
#define LIB_FTL_SYNCHRONIZATION_THREAD_CHECKER_H_

#include "lib/ftl/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "lib/ftl/macros.h"

namespace ftl {

// A simple class that records the identity of the thread that it was created
// on, and at later points can tell if the current thread is the same as its
// creation thread. This class is thread-safe.
//
// Note: Unlike Chromium's |base::ThreadChecker|, this is *not* Debug-only (so
// #ifdef it out if you want something Debug-only). (Rationale: Having a
// |CalledOnValidThread()| that lies in Release builds seems bad. Moreover,
// there's a small space cost to having even an empty class. )
class ThreadChecker final {
 public:
#if defined(OS_WIN)
  ThreadChecker() : self_(GetCurrentThreadId()) {}
  ~ThreadChecker() {}

  bool IsCreationThreadCurrent() const {
    return GetCurrentThreadId() == self_;
  }

 private:
  const DWORD self_;

#else
  ThreadChecker() : self_(pthread_self()) {}
  ~ThreadChecker() {}

  // Returns true if the current thread is the thread this object was created
  // on and false otherwise.
  bool IsCreationThreadCurrent() const {
    return !!pthread_equal(pthread_self(), self_);
  }

 private:
  const pthread_t self_;
#endif

  FTL_DISALLOW_COPY_AND_ASSIGN(ThreadChecker);
};

}  // namespace ftl

#endif  // LIB_FTL_SYNCHRONIZATION_THREAD_CHECKER_H_
