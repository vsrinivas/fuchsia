// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_TESTS_TRANSPORT_DEATH_TEST_HELPER_H_
#define LIB_FIDL_DRIVER_TESTS_TRANSPORT_DEATH_TEST_HELPER_H_

#include <lib/fit/function.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

namespace fidl_driver_testing {

// Utility to write death tests where the crashing statement happens on the
// current thread.
//
// This is useful for thread checker related tests which rely on
// specific thread identities. ASSERT_DEATH is not useful since it launches
// a new thread and runs assertions there.
class CurrentThreadExceptionHandler {
 public:
  // Register an exception handler for the current thread, then
  // Try running |statement| under the exception handler.
  void Try(fit::closure statement);

  // Wait for one exception to happen during |Try|.
  // The exception must be a software breakpoint.
  // Since this is blocking, it should be run from another thread.
  void WaitForOneSwBreakpoint();

 private:
  struct Exception {
    zx::process process;
    zx::thread thread;
    zx::exception handle;
    zx_exception_info_t info;
  };

  sync::Completion monitoring_exception_;
  zx::channel exception_channel_;
  Exception exception_ = {};
};

}  // namespace fidl_driver_testing

#endif  // LIB_FIDL_DRIVER_TESTS_TRANSPORT_DEATH_TEST_HELPER_H_
