// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_EXCEPTIONS_EXCEPTION_CATCHER_H_
#define LIB_TEST_EXCEPTIONS_EXCEPTION_CATCHER_H_

#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/status.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <list>

namespace test_exceptions {

// Test utility to catch and handle exceptions
//
// The simplest usage is to allow the constructor and destructor to start and
// stop. This will assert-fail if anything went wrong (e.g. an unexpected
// exception was found) which is fine for most tests:
//   {
//     ExceptionCatcher catcher(process);
//     ...
//   }
//
// If you want to be able to explicitly check for failure or avoid asserts,
// the ExceptionCatcher can be started and stopped manually instead, it
// just involves a bit of extra boilerplate:
//   ExceptionCatcher catcher;
//   ASSERT_OK(catcher.Start(process);
//   ...
//   ASSERT_OK(catcher.Stop());
//
// This class is thread-unsafe so external locking must be applied if it is
// used across threads.
class ExceptionCatcher {
 public:
  ExceptionCatcher() = default;

  // Calls Start() automatically and asserts that it succeeded.
  template <typename T>
  explicit ExceptionCatcher(const zx::task<T>& task);

  // Calls Stop() automatically and asserts that it succeeded.
  ~ExceptionCatcher();

  // Starts watching for exceptions on |task|. Can only be bound to a single
  // task at a time.
  template <typename T>
  zx_status_t Start(const zx::task<T>& task);

  // Stops watching for exceptions. Returns ZX_ERR_CANCELED if we got any
  // exceptions that were not handled via ExpectException().
  //
  // Any unhandled exceptions will be closed with TRY_NEXT.
  zx_status_t Stop();

  // Blocks until an exception is received. It then returns the exception. This will
  // return an error if the task exits without throwing an exception.
  zx::status<zx::exception> ExpectException();

  // Same as ExpectException() but only matches exceptions on |thread|.
  //
  // Any non-|thread| exceptions received will be held until they are
  // handled or the catcher is stopped.
  zx::status<zx::exception> ExpectException(const zx::thread& thread);

  // Same as ExpectException() but only matches exceptions on |process|.
  //
  // Any non-|process| exceptions received will be held until they are
  // handled or the catcher is stopped.
  zx::status<zx::exception> ExpectException(const zx::process& process);

 private:
  struct ActiveException {
    zx_exception_info_t info;
    zx::exception exception;
  };

  zx::status<zx::exception> ExpectException(zx_koid_t pid, zx_koid_t tid);

  zx::channel exception_channel_;
  std::list<ActiveException> active_exceptions_;
};

template <typename T>
ExceptionCatcher::ExceptionCatcher(const zx::task<T>& task) {
  zx_status_t status = Start(task);
  ZX_ASSERT_MSG(status == ZX_OK, "ExceptionCatcher::Start() failed (%s)",
                zx_status_get_string(status));
}

template <typename T>
zx_status_t ExceptionCatcher::Start(const zx::task<T>& task) {
  if (exception_channel_) {
    return ZX_ERR_ALREADY_BOUND;
  }
  return task.create_exception_channel(0, &exception_channel_);
}

}  // namespace test_exceptions

#endif  // LIB_TEST_EXCEPTIONS_EXCEPTION_CATCHER_H_
