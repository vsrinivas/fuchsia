// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <trace-reader/file_reader.h>

#include "garnet/bin/trace/tests/run_test.h"

const char kTracePath[] = "/bin/trace";
const char kChildPath[] = "/bin/trace";

// Note: /data is no longer large enough in qemu sessions
const char kOutputFile[] = "/tmp/test.trace";

// We don't enable all categories, we just need a kernel category we know we'll
// receive. Syscalls are a good choice. We also need the sched category to get
// syscall events (syscall enter/exit tracking requires thread tracking).
// And we also need irq events because syscalls are mapped to the "irq" group
// in the kernel.
// TODO(dje): This could use some cleanup.
const char kCategoriesArg[] =
    "--categories=kernel:syscall,kernel:sched,kernel:irq";

// Just print help text and exit.
const char kChildArg[] = "--help";

TEST(Ktrace, IntegrationTest) {
  zx::job job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  zx::process child;
  std::vector<std::string> argv{
      kTracePath, "record", "--spawn", "--binary", kCategoriesArg,
      std::string("--output-file=") + kOutputFile, kChildPath, kChildArg};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code),
            ZX_OK);
  EXPECT_EQ(return_code, 0);

  size_t record_count = 0;
  size_t syscall_count = 0;
  auto record_consumer = [&record_count, &syscall_count](trace::Record record){
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
    FXL_LOG(ERROR) << "While reading records got error: " << error.c_str();
    got_error = true;
  };

  std::unique_ptr<trace::FileReader> reader;
  ASSERT_TRUE(trace::FileReader::Create(kOutputFile,
                                        std::move(record_consumer),
                                        std::move(error_handler), &reader));
  reader->ReadFile();
  ASSERT_FALSE(got_error);

  FXL_LOG(INFO) << "Got " << record_count << " records, " << syscall_count
                << " syscalls";

  ASSERT_GT(syscall_count, 0u);
}

// Provide our own main so that --verbose,etc. are recognized.
int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
