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

const char kChildPath[] = "/bin/trace";

// We don't enable all categories, we just need a kernel category we know we'll
// receive. Syscalls are a good choice. We also need the sched category to get
// syscall events (syscall enter/exit tracking requires thread tracking).
// And we also need irq events because syscalls are mapped to the "irq" group
// in the kernel.
// TODO(dje): This could use some cleanup.
const char kCategoriesArg[] = "--categories=kernel:syscall,kernel:sched,kernel:irq";

// Just print help text and exit.
const char kChildArg[] = "--help";

// TODO(fxbug.dev/34893): Disabled until fixed.
TEST(Ktrace, DISABLED_IntegrationTest) {
  zx::job job{};  // -> default job
  std::vector<std::string> args{
      "record",
      "--spawn",
      "--binary",
      kCategoriesArg,
      std::string("--output-file=") + kSpawnedTestTmpPath + "/" + kRelativeOutputFilePath,
      kChildPath,
      kChildArg};
  ASSERT_TRUE(RunTraceAndWait(job, args));

  size_t record_count = 0;
  size_t syscall_count = 0;
  auto record_consumer = [&record_count, &syscall_count](trace::Record record) {
    ++record_count;

    // We're looking for ktrace records here, just enough to verify
    // ktrace_provider is connected and working.
    if (record.type() == trace::RecordType::kEvent) {
      const trace::Record::Event& event = record.GetEvent();
      if (event.type() == trace::EventType::kDurationComplete &&
          event.category == "kernel:syscall") {
        ++syscall_count;
      }
    }
  };

  bool got_error = false;
  auto error_handler = [&got_error](fbl::String error) {
    FX_LOGS(ERROR) << "While reading records got error: " << error.c_str();
    got_error = true;
  };

  std::unique_ptr<trace::FileReader> reader;
  ASSERT_TRUE(
      trace::FileReader::Create((std::string(kTestTmpPath) + "/" + kRelativeOutputFilePath).c_str(),
                                std::move(record_consumer), std::move(error_handler), &reader));
  reader->ReadFile();
  ASSERT_FALSE(got_error);

  FX_LOGS(INFO) << "Got " << record_count << " records, " << syscall_count << " syscalls";

  ASSERT_GT(syscall_count, 0u);
}

}  // namespace

}  // namespace test
}  // namespace tracing
