// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <pthread.h>

#include <thread>

#include <perftest/perftest.h>

#include "test_runner.h"

namespace {

// Test the round trip time for waking up threads using pthread condition
// variables (condvars).  Condvars are implemented using futexes, so we
// expect this to be a bit slower than FutexTest due to the overhead that
// pthread's condvar implementation adds.
class PthreadCondvarTest {
 public:
  PthreadCondvarTest() {
    FX_CHECK(pthread_mutex_init(&mutex_, nullptr) == 0);
    FX_CHECK(pthread_cond_init(&condvar1_, nullptr) == 0);
    FX_CHECK(pthread_cond_init(&condvar2_, nullptr) == 0);
    thread_ = std::thread([this]() { ThreadFunc(); });
  }

  ~PthreadCondvarTest() {
    // Tell the thread to shut down.
    FX_CHECK(pthread_mutex_lock(&mutex_) == 0);
    state_ = EXIT;
    FX_CHECK(pthread_cond_signal(&condvar1_) == 0);
    FX_CHECK(pthread_mutex_unlock(&mutex_) == 0);

    thread_.join();

    FX_CHECK(pthread_cond_destroy(&condvar1_) == 0);
    FX_CHECK(pthread_cond_destroy(&condvar2_) == 0);
    FX_CHECK(pthread_mutex_destroy(&mutex_) == 0);
  }

  void Run() {
    FX_CHECK(pthread_mutex_lock(&mutex_) == 0);
    // Wake the child.
    state_ = WAKE_CHILD;
    FX_CHECK(pthread_cond_signal(&condvar1_) == 0);
    // Wait for the reply.
    while (state_ != REPLY_TO_PARENT)
      FX_CHECK(pthread_cond_wait(&condvar2_, &mutex_) == 0);
    FX_CHECK(pthread_mutex_unlock(&mutex_) == 0);
  }

 private:
  void ThreadFunc() {
    FX_CHECK(pthread_mutex_lock(&mutex_) == 0);
    for (;;) {
      if (state_ == EXIT)
        break;
      if (state_ == WAKE_CHILD) {
        state_ = REPLY_TO_PARENT;
        FX_CHECK(pthread_cond_signal(&condvar2_) == 0);
      }
      FX_CHECK(pthread_cond_wait(&condvar1_, &mutex_) == 0);
    }
    FX_CHECK(pthread_mutex_unlock(&mutex_) == 0);
  }

  std::thread thread_;
  pthread_mutex_t mutex_;
  pthread_cond_t condvar1_;  // Signals from parent to child.
  pthread_cond_t condvar2_;  // Signals from child to parent.
  enum { INITIAL, WAKE_CHILD, REPLY_TO_PARENT, EXIT } state_ = INITIAL;
};

void RegisterTests() {
  fbenchmark::RegisterTest<PthreadCondvarTest>("RoundTrip_PthreadCondvar_SingleProcess");
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
