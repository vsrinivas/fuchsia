// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/cpu/insntrace/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <gtest/gtest.h>

#include "src/developer/tracing/lib/test_utils/run_program.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/test/test_settings.h"

namespace {

using ControllerSyncPtr = ::fuchsia::hardware::cpu::insntrace::ControllerSyncPtr;

const char kInsntraceDevicePath[] = "/dev/sys/cpu-trace/insntrace";

#ifdef __x86_64__

const char kInsntracePath[] = "/bin/insntrace";

// These files should be created when running insntrace.
const char* const kResultFileList[] = {
    "/tmp/ptout.cpuid",
    "/tmp/ptout.ktrace",
    "/tmp/ptout.ptlist",
};

// FIXME(61706): This test has been failing on the core.x64 release clang canary builder for some
// time, but debugging/bisecting for a faulty Clang commit has been pretty difficult. Since this
// seems to be the only test failing, we will temporarily disable the test to facilitate the roll,
// then continue to debug this once the roll lands and re-enable the test.
TEST(Insntrace, DISABLED_Control) {
  zx::job job{};  // -> default job

  // A note on file sizes:
  // The default size of the output file is 256K. With 4 cpus that's 1MB
  // which is fine. There is also the ktrace buffer which is 32MB by default.

  std::vector<std::string> start_argv{kInsntracePath, "--control", "init", "start"};
  ASSERT_TRUE(tracing::test::RunProgramAndWait(job, start_argv, 0, nullptr));

  // Give tracing something to trace.
  std::vector<std::string> help_argv{kInsntracePath, "--help"};
  ASSERT_TRUE(tracing::test::RunProgramAndWait(job, help_argv, 0, nullptr));

  std::vector<std::string> stop_argv{kInsntracePath, "--control", "stop", "dump", "reset"};
  ASSERT_TRUE(tracing::test::RunProgramAndWait(job, stop_argv, 0, nullptr));

  // There's not much more we can do at this point, beyond verifying the
  // expected files exist. Examining them requires the reader-library which
  // is a host-side tool.
  for (const auto& path : kResultFileList) {
    EXPECT_EQ(access(path, R_OK), 0) << "Missing: " << path;
    unlink(path);
  }

  unsigned num_cpus = zx_system_get_num_cpus();
  static const char kCpuOutputPathPattern[] = "/tmp/ptout.cpu%u.pt";
  char cpu_output_path[sizeof(kCpuOutputPathPattern) + 10];
  for (unsigned cpu = 0; cpu < num_cpus; ++cpu) {
    snprintf(cpu_output_path, sizeof(cpu_output_path), kCpuOutputPathPattern, cpu);
    EXPECT_EQ(access(cpu_output_path, R_OK), 0) << "Missing: " << cpu_output_path;
    unlink(cpu_output_path);
  }
}

#endif  // __x86_64__

ControllerSyncPtr OpenDevice() {
  ControllerSyncPtr controller_ptr;
  zx_status_t status = fdio_service_connect(kInsntraceDevicePath,
                                            controller_ptr.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error connecting to " << kInsntraceDevicePath << ": " << status;
    return ControllerSyncPtr();
  }
  return controller_ptr;
}

bool IsSupported() {
  ControllerSyncPtr ipt{OpenDevice()};
  if (!ipt) {
    return false;
  }
  // TODO(dje): Need fidl interface to query device properties.
  ::fuchsia::hardware::cpu::insntrace::Controller_Terminate_Result result;
  zx_status_t status = ipt->Terminate(&result);
  if (status != ZX_OK) {
    FX_VLOGS(1) << "Is-supported proxy(terminate) failed: " << status;
    return false;
  }
  FX_DCHECK(result.is_err());
  if (result.is_err()) {
    FX_VLOGS(1) << "Is-supported proxy(terminate) received: " << result.err();
    return result.err() == ZX_ERR_BAD_STATE;
  }
  FX_VLOGS(1) << "Is-supported proxy(terminate) failed";
  return false;
}

}  // namespace

// Provide our own main so that we can do an early-exit if instruction
// tracing is not supported.
int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetTestSettings(cl))
    return EXIT_FAILURE;

  // Early exit if there is no insntrace device.
  if (!IsSupported()) {
    FX_LOGS(INFO) << "Insntrace device not supported";
    return EXIT_SUCCESS;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
