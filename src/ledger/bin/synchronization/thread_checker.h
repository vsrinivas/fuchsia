// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNCHRONIZATION_THREAD_CHECKER_H_
#define SRC_LEDGER_BIN_SYNCHRONIZATION_THREAD_CHECKER_H_

#include <pthread.h>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

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
  ThreadChecker() : self_(pthread_self()) {}
  ThreadChecker(const ThreadChecker&) = delete;
  ThreadChecker& operator=(const ThreadChecker&) = delete;
  ~ThreadChecker() {}

  // Returns true if the current thread is the thread this object was created
  // on and false otherwise.
  bool IsCreationThreadCurrent() const { return !!pthread_equal(pthread_self(), self_); }

 private:
  const pthread_t self_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_SYNCHRONIZATION_THREAD_CHECKER_H_
