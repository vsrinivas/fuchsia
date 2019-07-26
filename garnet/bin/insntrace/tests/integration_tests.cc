// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <fuchsia/hardware/cpu/insntrace/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fdio/directory.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <src/developer/tracing/lib/test_utils/spawn_and_wait.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/test/test_settings.h>
#include <zircon/syscalls.h>

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

TEST(Insntrace, TraceProgram) {
  zx::job job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  // A note on file sizes:
  // The default size of the output file is 256K. With 4 cpus that's 1MB
  // which is fine. There is also the ktrace buffer which is 32MB by default.

  zx::process child;
  std::vector<std::string> argv{kInsntracePath, kInsntracePath, "--help"};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code), ZX_OK);
  EXPECT_EQ(return_code, 0);

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
    FXL_LOG(ERROR) << "Error connecting to " << kInsntraceDevicePath << ": " << status;
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
    FXL_VLOG(1) << "Is-supported proxy(terminate) failed: " << status;
    return false;
  }
  FXL_DCHECK(result.is_err());
  if (result.is_err()) {
    FXL_VLOG(1) << "Is-supported proxy(terminate) received: " << result.err();
    return result.err() == ZX_ERR_BAD_STATE;
  }
  FXL_VLOG(1) << "Is-supported proxy(terminate) failed";
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
    FXL_LOG(INFO) << "Insntrace device not supported";
    return EXIT_SUCCESS;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
