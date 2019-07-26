// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/test-exceptions/exception-catcher.h>

#include <zircon/errors.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zxtest/zxtest.h>

namespace test_exceptions {

namespace {

// Helper to easily create and kill threads to reduce boilerplate.
class TestThread {
 public:
  TestThread() {
    EXPECT_OK(zx::thread::create(*zx::process::self(), "test", strlen("test"), 0, &thread_));
  }

  ~TestThread() {
    if (thread_) {
      // It's OK if an ExceptionCatcher already killed this thread,
      // killing a task multiple times has no effect.
      EXPECT_OK(thread_.kill());
    }
  }

  const zx::thread& get() const { return thread_; }

  zx_status_t StartAndCrash() {
    // Passing 0 for sp and pc crashes the thread immediately.
    return thread_.start(nullptr, 0, 0, 0);
  }

  // Blocks until the thread is in an exception.
  zx_status_t WaitUntilInException() {
    while (1) {
      zx_info_thread_t info;
      zx_status_t status = thread_.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        return status;
      }

      if (info.wait_exception_channel_type == ZX_EXCEPTION_CHANNEL_TYPE_NONE) {
        zx::nanosleep(zx::deadline_after(zx::msec(1)));
      } else {
        return ZX_OK;
      }
    }
  }

 private:
  zx::thread thread_;
};

TEST(ExceptionCatcher, NoExceptions) {
  TestThread thread;

  ExceptionCatcher catcher(thread.get());
}

TEST(ExceptionCatcher, NoExceptionsManualStartStop) {
  TestThread thread;

  ExceptionCatcher catcher;
  EXPECT_OK(catcher.Start(thread.get()));
  EXPECT_OK(catcher.Stop());
}

TEST(ExceptionCatcher, MultipleStartFailure) {
  TestThread thread, thread2;

  ExceptionCatcher catcher;
  EXPECT_OK(catcher.Start(thread.get()));
  EXPECT_NOT_OK(catcher.Start(thread2.get()));
}

TEST(ExceptionCatcher, ChannelInUseFailure) {
  TestThread thread;

  ExceptionCatcher catcher, catcher2;
  EXPECT_OK(catcher.Start(thread.get()));
  EXPECT_NOT_OK(catcher2.Start(thread.get()));
}

TEST(ExceptionCatcher, CatchException) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());

  ASSERT_OK(thread.StartAndCrash());
  EXPECT_OK(catcher.ExpectException());
}

TEST(ExceptionCatcher, CatchThreadException) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());

  ASSERT_OK(thread.StartAndCrash());
  EXPECT_OK(catcher.ExpectException(thread.get()));
}

TEST(ExceptionCatcher, CatchProcessException) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());

  ASSERT_OK(thread.StartAndCrash());
  EXPECT_OK(catcher.ExpectException(*zx::process::self()));
}

TEST(ExceptionCatcher, CatchMultipleExceptions) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  for ([[maybe_unused]] auto& thread : threads) {
    EXPECT_OK(catcher.ExpectException());
  }
}

TEST(ExceptionCatcher, CatchMultipleThreadExceptions) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  for (auto& thread : threads) {
    EXPECT_OK(catcher.ExpectException(thread.get()));
  }
}

TEST(ExceptionCatcher, CatchMultipleProcessExceptions) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  for ([[maybe_unused]] auto& thread : threads) {
    EXPECT_OK(catcher.ExpectException(*zx::process::self()));
  }
}

TEST(ExceptionCatcher, CatchMultipleThreadExceptionsAnyOrder) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  EXPECT_OK(catcher.ExpectException(threads[1].get()));
  EXPECT_OK(catcher.ExpectException(threads[3].get()));
  EXPECT_OK(catcher.ExpectException(threads[0].get()));
  EXPECT_OK(catcher.ExpectException(threads[2].get()));
}

TEST(ExceptionCatcher, CatchExceptionFromKilledThread) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());
  ASSERT_OK(thread.StartAndCrash());
  ASSERT_OK(thread.WaitUntilInException());
  ASSERT_OK(thread.get().kill());
  ASSERT_OK(thread.get().wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr));

  // Exception should still be handled properly even if the exception
  // channel has since been closed.
  EXPECT_OK(catcher.ExpectException());
}

TEST(ExceptionCatcher, UncaughtExceptionFailure) {
  // Catch the exception again at the process level so it doesn't filter
  // up to the system crash handler and kill our whole process.
  ExceptionCatcher process_catcher(*zx::process::self());

  TestThread thread;
  ExceptionCatcher catcher(thread.get());
  ASSERT_OK(thread.StartAndCrash());
  ASSERT_OK(thread.WaitUntilInException());

  EXPECT_EQ(ZX_ERR_CANCELED, catcher.Stop());

  EXPECT_OK(process_catcher.ExpectException(thread.get()));
}

TEST(ExceptionCatcher, ThreadTerminatedFailure) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());
  ASSERT_OK(thread.StartAndCrash());
  ASSERT_OK(catcher.ExpectException(thread.get()));

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, catcher.ExpectException(thread.get()));
}

}  // namespace

}  // namespace test_exceptions
