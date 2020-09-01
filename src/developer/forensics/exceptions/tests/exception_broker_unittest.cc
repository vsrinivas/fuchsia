// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_broker.h"

#include <array>

#include <gtest/gtest.h>

#include "src/developer/forensics/exceptions/tests/crasher_wrapper.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace exceptions {
namespace {

bool RetrieveExceptionContext(ExceptionContext* pe) {
  // Create a process that crashes and obtain the relevant handles and exception.
  // By the time |SpawnCrasher| has returned, the process has already thrown an exception.
  if (!SpawnCrasher(pe))
    return false;

  // We mark the exception to be handled. We need this because we pass on the exception to the
  // handler, which will resume it before we get the control back. If we don't mark it as handled,
  // the exception will bubble out of our environment.
  return MarkExceptionAsHandled(pe);
}

size_t NumSubprocesses() {
  size_t actual{0};
  size_t avail{0};
  std::array<zx_koid_t, 8> children;
  children.fill(ZX_KOID_INVALID);
  zx_object_get_info(zx_job_default(), ZX_INFO_JOB_PROCESSES, children.data(),
                     children.size() * sizeof(zx_koid_t), &actual, &avail);

  // Account for the process the test is runnning in.
  return actual - 1;
}

using PendingExceptionTest = UnitTestFixture;

TEST_F(PendingExceptionTest, ExceptionExpires) {
  const zx::duration ttl{zx::sec(1)};

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  ASSERT_TRUE(exception.exception.is_valid());
  PendingException pending_exception(dispatcher(), ttl, std::move(exception.exception));

  RunLoopFor(ttl);

  ASSERT_FALSE(pending_exception.TakeException().is_valid());

  // We kill the job. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

using ProcessHandlerTest = UnitTestFixture;

TEST_F(ProcessHandlerTest, ManagesSubprocessLifetime) {
  {
    ProcessHandler process_handler(dispatcher(), [] {});
    ASSERT_EQ(NumSubprocesses(), 0u);

    process_handler.Handle("process-name", 0u, zx::exception{});

    ASSERT_EQ(NumSubprocesses(), 1u);
  }

  while (NumSubprocesses() > 0) {
    RunLoopUntilIdle();
  }

  EXPECT_EQ(NumSubprocesses(), 0u);
}

TEST_F(ProcessHandlerTest, OnAvailableCalled) {
  bool available = false;
  ProcessHandler process_handler(dispatcher(), [&available] { available = true; });

  process_handler.Handle("process-name", 0u, zx::exception{});

  while (!available) {
    RunLoopUntilIdle();
  }
}

}  // namespace
}  // namespace exceptions
}  // namespace forensics
