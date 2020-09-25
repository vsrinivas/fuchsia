// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>

#include <gtest/gtest.h>
#include <trace-reader/file_reader.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"

namespace tracing {
namespace test {

namespace {

const char kAppUrl[] = "fuchsia-pkg://fuchsia.com/trace_tests#meta/provider_destruction_app.cmx";

// We don't enable all categories, we just need a kernel category we know we'll
// receive. Syscalls are a good choice. We also need the sched category to get
// syscall events (syscall enter/exit tracking requires thread tracking).
// And we also need irq events because syscalls are mapped to the "irq" group
// in the kernel.
// TODO(dje): This could use some cleanup.
const char kCategoriesArg[] = "--categories=" CATEGORY_NAME;

// Try this many times to exercise shutdown.
// This has been more than enough to trigger fxbug.dev/23108 in practice.
constexpr size_t kNumIterations = 50;

TEST(ProviderDestruction, StressTest) {
  zx::job job{};  // -> default job
  for (size_t i = 0; i < kNumIterations; ++i) {
    std::vector<std::string> args{
        "record", kCategoriesArg,
        std::string("--output-file=") + kSpawnedTestTmpPath + "/" + kRelativeOutputFilePath,
        kAppUrl};
    ASSERT_TRUE(RunTraceAndWait(job, args));

    size_t num_events;
    ASSERT_TRUE(VerifyTestEventsFromJson(std::string(kTestTmpPath) + "/" + kRelativeOutputFilePath,
                                         &num_events));
    FX_VLOGS(1) << "Got " << num_events << " events";
  }
}

}  // namespace

}  // namespace test
}  // namespace tracing
