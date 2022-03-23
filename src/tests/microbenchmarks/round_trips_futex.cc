// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <thread>

#include <perftest/perftest.h>

#include "assert.h"
#include "test_runner.h"

namespace {

// Test the round trip time for waking up threads using Zircon futexes.
// Note that Zircon does not support cross-process futexes, only
// within-process futexes, so there is no multi-process version of this
// test case.
class FutexTest {
 public:
  FutexTest() {
    thread_ = std::thread([this]() { ThreadFunc(); });
  }

  ~FutexTest() {
    Wake(&futex1_, 2);  // Tell the thread to shut down.
    thread_.join();
  }

  void Run() {
    Wake(&futex1_, 1);
    FX_CHECK(!Wait(&futex2_));
  }

 private:
  void ThreadFunc() {
    for (;;) {
      if (Wait(&futex1_))
        break;
      Wake(&futex2_, 1);
    }
  }

  void Wake(volatile int* ptr, int wake_value) {
    *ptr = wake_value;
    ASSERT_OK(zx_futex_wake(const_cast<int*>(ptr), 1));
  }

  bool Wait(volatile int* ptr) {
    for (;;) {
      int val = *ptr;
      if (val != 0) {
        // We were signaled.  Reset the state to unsignaled.
        *ptr = 0;
        // Return whether we got a request to shut down.
        return val == 2;
      }
      zx_status_t status =
          zx_futex_wait(const_cast<int*>(ptr), val, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
      FX_CHECK(status == ZX_OK || status == ZX_ERR_BAD_STATE);
    }
  }

  std::thread thread_;
  volatile int futex1_ = 0;  // Signals from client to server.
  volatile int futex2_ = 0;  // Signals from server to client.
};

void RegisterTests() { fbenchmark::RegisterTest<FutexTest>("RoundTrip_Futex_SingleProcess"); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
