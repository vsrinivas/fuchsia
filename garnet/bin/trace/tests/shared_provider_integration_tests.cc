// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <src/lib/fxl/logging.h>
#include <trace-reader/file_reader.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"

const char kTracePath[] = "/bin/trace";

const char kAppUrl[] = "fuchsia-pkg://fuchsia.com/trace_tests#meta/shared_provider_app.cmx";

// Note: /data is no longer large enough in qemu sessions
const char kOutputFile[] = "/tmp/test.trace";

// We don't enable all categories, we just need a kernel category we know we'll
// receive. Syscalls are a good choice. We also need the sched category to get
// syscall events (syscall enter/exit tracking requires thread tracking).
// And we also need irq events because syscalls are mapped to the "irq" group
// in the kernel.
// TODO(dje): This could use some cleanup.
const char kCategoriesArg[] = "--categories=" CATEGORY_NAME;

namespace {

TEST(SharedProvider, IntegrationTest) {
  zx::job job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  zx::process child;
  std::vector<std::string> argv{kTracePath, "record", kCategoriesArg,
                                std::string("--output-file=") + kOutputFile, kAppUrl};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code), ZX_OK);
  EXPECT_EQ(return_code, 0);

  size_t num_events;
  EXPECT_TRUE(VerifyTestEvents(kOutputFile, &num_events));
  FXL_VLOG(1) << "Got " << num_events << " events";
}

}  // namespace
