// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/test-exceptions/exception-catcher.h>
#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <thread>

#include <zxtest/zxtest.h>

namespace test_exceptions {

namespace {

// Helper to easily create and kill threads to reduce boilerplate.
class TestThread {
 public:
  TestThread() {
    EXPECT_OK(zx::thread::create(*zx::process::self(), "test", strlen("test"), 0, &thread_));
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
  zx::status<zx::exception> result = catcher.ExpectException();
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
}

TEST(ExceptionCatcher, CatchThreadException) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());

  ASSERT_OK(thread.StartAndCrash());
  zx::status<zx::exception> result = catcher.ExpectException(thread.get());
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
}

TEST(ExceptionCatcher, CatchProcessException) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());

  ASSERT_OK(thread.StartAndCrash());
  zx::status<zx::exception> result = catcher.ExpectException(*zx::process::self());
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
}

TEST(ExceptionCatcher, CatchMultipleExceptions) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  for ([[maybe_unused]] auto& thread : threads) {
    zx::status<zx::exception> result = catcher.ExpectException();
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
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
    zx::status<zx::exception> result = catcher.ExpectException(thread.get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
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
    zx::status<zx::exception> result = catcher.ExpectException(*zx::process::self());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }
}

TEST(ExceptionCatcher, CatchMultipleThreadExceptionsAnyOrder) {
  ExceptionCatcher catcher(*zx::process::self());

  TestThread threads[4];
  for (auto& thread : threads) {
    ASSERT_OK(thread.StartAndCrash());
    ASSERT_OK(thread.WaitUntilInException());
  }

  {
    zx::status<zx::exception> result = catcher.ExpectException(threads[1].get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }

  {
    zx::status<zx::exception> result = catcher.ExpectException(threads[3].get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }

  {
    zx::status<zx::exception> result = catcher.ExpectException(threads[0].get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }

  {
    zx::status<zx::exception> result = catcher.ExpectException(threads[2].get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }
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

  zx::status<zx::exception> result = process_catcher.ExpectException(thread.get());
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
}

TEST(ExceptionCatcher, ThreadTerminatedFailure) {
  TestThread thread;
  ExceptionCatcher catcher(thread.get());
  ASSERT_OK(thread.StartAndCrash());
  {
    zx::status<zx::exception> result = catcher.ExpectException(thread.get());
    ASSERT_TRUE(result.is_ok());
    ASSERT_OK(ExitExceptionZxThread(std::move(result.value())));
  }

  zx::status<zx::exception> result = catcher.ExpectException(thread.get());
  ASSERT_EQ(result.status_value(), ZX_ERR_PEER_CLOSED);
}

void crash_function() {
  volatile int* bad_address = nullptr;
  *bad_address = 5;
}

int thrd_crash_function(void* arg) {
  crash_function();
  return 0;
}

void* pthread_crash_function(void* arg) {
  crash_function();
  return nullptr;
}

TEST(ExceptionCatcher, CThreadExit) {
  zx::unowned_process process = zx::process::self();
  ExceptionCatcher catcher(*process);

  thrd_t thread;
  ASSERT_EQ(thrd_create(&thread, thrd_crash_function, nullptr), thrd_success);

  auto result = catcher.ExpectException();
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(catcher.Stop());

  ASSERT_OK(ExitExceptionCThread(std::move(result.value())));

  ASSERT_EQ(thrd_join(thread, nullptr), thrd_success);
}

TEST(ExceptionCatcher, PThreadExit) {
  zx::unowned_process process = zx::process::self();
  ExceptionCatcher catcher(*process);

  pthread_t thread;
  ASSERT_EQ(pthread_create(&thread, nullptr, &pthread_crash_function, nullptr), 0);

  auto result = catcher.ExpectException();
  ASSERT_TRUE(result.is_ok());
  ASSERT_OK(catcher.Stop());

  ASSERT_OK(ExitExceptionPThread(std::move(result.value())));

  ASSERT_EQ(pthread_join(thread, nullptr), 0);
}

}  // namespace

}  // namespace test_exceptions
