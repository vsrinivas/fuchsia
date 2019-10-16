// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/zx/job.h>
#include <src/lib/fxl/logging.h>
#include <trace-reader/file_reader.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"

const char kAppUrl[] = "fuchsia-pkg://fuchsia.com/trace_tests#meta/shared_provider_app.cmx";

// Note: /data is no longer large enough in qemu sessions
const char kRelativeOutputFilePath[] = "test.trace";

// We don't enable all categories, we just need a kernel category we know we'll
// receive. Syscalls are a good choice. We also need the sched category to get
// syscall events (syscall enter/exit tracking requires thread tracking).
// And we also need irq events because syscalls are mapped to the "irq" group
// in the kernel.
// TODO(dje): This could use some cleanup.
const char kCategoriesArg[] = "--categories=" CATEGORY_NAME;

namespace {

TEST(SharedProvider, IntegrationTest) {
  zx::job job{};  // -> default job
  std::vector<std::string> args{
    "record",
    kCategoriesArg,
    std::string("--output-file=") + kSpawnedTestTmpPath + "/" + kRelativeOutputFilePath,
    kAppUrl};
  ASSERT_TRUE(RunTraceAndWait(job, args));

  size_t num_events;
  EXPECT_TRUE(VerifyTestEvents(std::string(kTestTmpPath) + "/" + kRelativeOutputFilePath,
                               &num_events));
  FXL_VLOG(1) << "Got " << num_events << " events";
}

}  // namespace
