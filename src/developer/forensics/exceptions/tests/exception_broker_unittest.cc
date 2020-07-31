// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/exception_broker.h"

#include <lib/syslog/cpp/macros.h>

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
  zx_object_get_info(zx_job_default(), ZX_INFO_JOB_PROCESSES, children.data(), children.size(),
                     &actual, &avail);

  FX_CHECK(actual == avail);

  // Account for the process the test is runnning in.
  return actual - 1;
}

using ExceptionBrokerTest = UnitTestFixture;

TEST_F(ExceptionBrokerTest, ExecutesCallback) {
  auto broker =
      ExceptionBroker::Create(dispatcher(), /*max_num_handlers=*/1u, /*exception_ttl=*/zx::hour(1));

  // Create the exception.
  ExceptionContext exception;
  ASSERT_TRUE(RetrieveExceptionContext(&exception));

  bool called{false};
  broker->OnException(std::move(exception.exception), {}, [&called] { called = true; });

  while (!called) {
    RunLoopUntilIdle();
  }

  EXPECT_EQ(NumSubprocesses(), 0u);

  // We kill the job. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

TEST_F(ExceptionBrokerTest, LimitsNumSubprocesses) {
  auto broker =
      ExceptionBroker::Create(dispatcher(), /*max_num_handlers=*/1u, /*exception_ttl=*/zx::hour(1));

  ExceptionContext exceptions[2];
  ASSERT_TRUE(RetrieveExceptionContext(exceptions + 0));
  ASSERT_TRUE(RetrieveExceptionContext(exceptions + 1));

  bool called1{false};
  broker->OnException(std::move(exceptions[0].exception), {}, [&called1] { called1 = true; });

  bool called2{false};
  broker->OnException(std::move(exceptions[1].exception), {}, [&called2] { called2 = true; });

  while (!called1) {
    RunLoopUntilIdle();
  }

  // This should only ever fail if spawing the handler processes fails because the callback for the
  // second call to OnException would be immediately posted on the loop when broker fails to create
  // the first handler. This results in |called2| being set to true during the call to
  // RunLoopUntilIdle() above.
  ASSERT_FALSE(called2);

  while (!called2) {
    RunLoopUntilIdle();
  }

  EXPECT_EQ(NumSubprocesses(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exceptions[0].job.kill();
  exceptions[1].job.kill();
}

}  // namespace
}  // namespace exceptions
}  // namespace forensics
