// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/thread.h>
#include <zxtest/zxtest.h>

namespace {

constexpr zx_signals_t kChannelReadySignal = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kBacktraceReturnedSignal = ZX_USER_SIGNAL_1;

TEST(BacktraceRequest, RequestAndResume) {
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::channel exception_channel;
  std::thread thread([&] {
    // Attach an exception handler so we can resume the request thread
    // locally without going to up the system crash service.
    ASSERT_OK(zx::thread::self()->create_exception_channel(0, &exception_channel));
    ASSERT_OK(event.signal(0, kChannelReadySignal));

    // Request the backtrace, then once it returns flip the signal to prove
    // we got control back at the right place.
    backtrace_request();
    ASSERT_OK(event.signal(0, kBacktraceReturnedSignal));
  });

  ASSERT_OK(event.wait_one(kChannelReadySignal, zx::time::infinite(), nullptr));

  // Pull out the exception and all the state we need.
  zx_exception_info_t info;
  zx::exception exception;
  zx::thread exception_thread;
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr));
  ASSERT_OK(exception.get_thread(&exception_thread));
  ASSERT_OK(exception_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  // Make sure this is a backtrace and clean it up.
  ASSERT_TRUE(is_backtrace_request(info.type, &regs));
  ASSERT_OK(cleanup_backtrace_request(exception_thread.get(), &regs));

  // Resume the thread, it should pick up where it left off.
  uint32_t handled = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &handled, sizeof(handled)));
  exception.reset();

  ASSERT_OK(event.wait_one(kBacktraceReturnedSignal, zx::time::infinite(), nullptr));
  thread.join();
}

TEST(BacktraceRequest, RequestAndResumeManyThreads) {
  // We only care that at least one thread triggers it before creating the main thread.
  constexpr zx_signals_t kWaitThreadReady = ZX_USER_SIGNAL_2;
  constexpr zx_signals_t kTestDoneSignal = ZX_USER_SIGNAL_3;
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  // Create 5 threads that will wait until the test is done.
  constexpr int kWaitThreadCount = 5;
  std::thread wait_threads[kWaitThreadCount];
  for (int i = 0; i < kWaitThreadCount; i++) {
    wait_threads[i] = std::thread([&]() {
      // Signal we're ready and wait for the test to be done.
      // It doesn't matter that the signal is already asserted when another thread comes.
      ASSERT_OK(event.signal(0, kWaitThreadReady));
      ASSERT_OK(event.wait_one(kTestDoneSignal, zx::time::infinite(), nullptr));
    });
  }

  // Wait for one of the wait threads to be ready.
  ASSERT_OK(event.wait_one(kWaitThreadReady, zx::time::infinite(), nullptr));

  zx::channel exception_channel;
  std::thread thread([&] {
    // Attach an exception handler so we can resume the request thread
    // locally without going to up the system crash service.
    ASSERT_OK(zx::thread::self()->create_exception_channel(0, &exception_channel));
    ASSERT_OK(event.signal(0, kChannelReadySignal));

    // Request the backtrace, then once it returns flip the signal to prove
    // we got control back at the right place.
    backtrace_request();
    ASSERT_OK(event.signal(0, kBacktraceReturnedSignal));
  });

  ASSERT_OK(event.wait_one(kChannelReadySignal, zx::time::infinite(), nullptr));

  // Pull out the exception and all the state we need.
  zx_exception_info_t info;
  zx::exception exception;
  zx::thread exception_thread;
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr));
  ASSERT_OK(exception.get_thread(&exception_thread));
  ASSERT_OK(exception_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  // Make sure this is a backtrace and clean it up.
  ASSERT_TRUE(is_backtrace_request(info.type, &regs));
  ASSERT_OK(cleanup_backtrace_request(exception_thread.get(), &regs));

  // Resume the thread, it should pick up where it left off.
  uint32_t handled = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &handled, sizeof(handled)));
  exception.reset();

  ASSERT_OK(event.wait_one(kBacktraceReturnedSignal, zx::time::infinite(), nullptr));
  thread.join();

  // Tell all the other threads we're done.
  ASSERT_OK(event.signal(0, kTestDoneSignal));
  for (int i = 0; i < kWaitThreadCount; i++) {
    wait_threads[i].join();
  }
}


TEST(BacktraceRequest, IgnoreNormalException) {
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::channel exception_channel;
  const void* exit_address = nullptr;

  std::thread thread([&] {
    exit_address = &&exit;   // clang and gcc extension to obtain address of a label.
    ASSERT_OK(zx::thread::self()->create_exception_channel(0, &exception_channel));
    ASSERT_OK(event.signal(0, kChannelReadySignal));
    // Segfault.
    volatile int* p = 0;
    *p = 0;
  exit:
    ;
  });

  ASSERT_OK(event.wait_one(kChannelReadySignal, zx::time::infinite(), nullptr));

  zx_exception_info_t info;
  zx::exception exception;
  zx::thread exception_thread;
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                   nullptr, nullptr));
  ASSERT_OK(exception.get_thread(&exception_thread));
  ASSERT_OK(exception_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  ASSERT_FALSE(is_backtrace_request(info.type, &regs));

  // Move the program counter past the exception and resume. The thread should exit and
  // clean up normally.
#ifdef __aarch64__
  regs.pc = reinterpret_cast<size_t>(exit_address);
#elif defined(__x86_64__)
  regs.rip = reinterpret_cast<size_t>(exit_address);
#else
#error "what machine?"
#endif

  ASSERT_OK(exception_thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  uint32_t handled = ZX_EXCEPTION_STATE_HANDLED;
  ASSERT_OK(exception.set_property(ZX_PROP_EXCEPTION_STATE, &handled, sizeof(handled)));
  exception.reset();

  thread.join();
}

}  // namespace
