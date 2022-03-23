// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <thread>

#include <perftest/perftest.h>

#include "test_runner.h"

#if defined(__Fuchsia__)

#include <zircon/syscalls.h>

#include "assert.h"

#elif defined(__linux__)

#include <sys/syscall.h>

#include <linux/futex.h>

#else
#error "Unsupported operating system"
#endif

namespace {

#if defined(__Fuchsia__)

void FutexWake(volatile int* ptr) { ASSERT_OK(zx_futex_wake(const_cast<int*>(ptr), 1)); }

void FutexWait(volatile int* ptr, int expected_value) {
  zx_status_t status =
      zx_futex_wait(const_cast<int*>(ptr), expected_value, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
  // zx_futex_wait() returns ZX_ERR_BAD_STATE if *ptr != expected_value, or
  // ZX_OK if woken by a zx_futex_wake() call.
  FX_CHECK(status == ZX_OK || status == ZX_ERR_BAD_STATE);
}

#elif defined(__linux__)

void FutexWake(volatile int* ptr) {
  long woken_count =
      syscall(__NR_futex, ptr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, nullptr, nullptr, 0);
  FX_CHECK(woken_count >= 0);
}

void FutexWait(volatile int* ptr, int expected_value) {
  long result = syscall(__NR_futex, ptr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected_value, nullptr,
                        nullptr, 0);
  // FUTEX_WAIT returns the error EAGAIN if *ptr != expected_value, or 0
  // (success) if woken by a FUTEX_WAKE call.
  FX_CHECK(result == 0 || (result < 0 && errno == EAGAIN));
}

#else
#error "Unsupported operating system"
#endif

// Test the round trip time for waking up threads using futexes.
//
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
    FutexWake(ptr);
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
      FutexWait(ptr, val);
    }
  }

  std::thread thread_;
  volatile int futex1_ = 0;  // Signals from client to server.
  volatile int futex2_ = 0;  // Signals from server to client.
};

void RegisterTests() { fbenchmark::RegisterTest<FutexTest>("RoundTrip_Futex_SingleProcess"); }
PERFTEST_CTOR(RegisterTests)

}  // namespace
