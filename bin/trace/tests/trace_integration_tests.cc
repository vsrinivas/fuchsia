// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <zircon/status.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include "garnet/bin/trace/tests/run_test.h"
#include "gtest/gtest.h"

// Note: /data is no longer large enough in qemu sessions
const char kOutputFilePath[] = "/tmp/test.trace";

static void RunAndVerify(const char* tspec_path) {
  ASSERT_TRUE(RunTspec(tspec_path, kOutputFilePath));
  ASSERT_TRUE(VerifyTspec(tspec_path, kOutputFilePath));
}

TEST(Oneshot, FillBuffer) {
  RunAndVerify("/pkg/data/oneshot.tspec");
}

TEST(Circular, FillBuffer) {
  RunAndVerify("/pkg/data/circular.tspec");
}

TEST(Streaming, FillBuffer) {
  RunAndVerify("/pkg/data/streaming.tspec");
}

// We currently don't support two providers in one process (and there are no
// current plans to). But if someone accidentally creates such a beast, we
// want to handle it gracefully.
class TwoProvidersInSameProcess : public ::testing::Test {
 protected:
  // Path of the program that starts two providers.
  static const char kTwoProviderPath[];

  const zx::process& provider_process() const { return provider_process_; }

  void SetUp() override {
    zx::job job{}; // -> default job
    argv_.push_back(kTwoProviderPath);
    AppendLoggingArgs(&argv_, "");
    zx::eventpair their_event;
    auto status = zx::eventpair::create(0u, &our_event_, &their_event);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error creating event pair: "
                     << zx_status_get_string(status);
      return;
    }
    status = SpawnProgram(job, argv_, their_event.release(),
                          &provider_process_);
    if (status != ZX_OK) {
      TearDown();
      return;
    }
    // Wait for the provider to be ready.
    zx_wait_item_t wait_items[2] = {
      {
        .handle = provider_process_.get(),
        .waitfor = ZX_PROCESS_TERMINATED,
        .pending = 0,
      },
      {
        .handle = our_event_.get(),
        .waitfor = ZX_EVENTPAIR_SIGNALED | ZX_EVENTPAIR_PEER_CLOSED,
        .pending = 0,
      },
    };
    status = our_event_.wait_many(
      &wait_items[0], 2, zx::deadline_after(zx::duration(kTestTimeout)));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed waiting for provider process to start\n";
      TearDown();
    }
    FXL_LOG(INFO) << "Two-provider provider started";
  }

  void TearDown() override {
    if (provider_process_.is_valid()) {
      our_event_.reset();
      int exit_code;
      auto status = WaitAndGetExitCode(argv_[0], provider_process_,
                                       &exit_code);
      EXPECT_EQ(status, ZX_OK);
      if (status == ZX_OK) {
        EXPECT_EQ(exit_code, 0);
      }
      provider_process_.reset();
      FXL_LOG(INFO) << "Two-provider provider terminated";
    }
  }

 private:
  zx::eventpair our_event_;
  zx::process provider_process_;
  std::vector<std::string> argv_;
};

const char TwoProvidersInSameProcess::kTwoProviderPath[] =
  "/pkg/bin/two_provider_provider";

TEST_F(TwoProvidersInSameProcess, ErrorHandling) {
  ASSERT_TRUE(provider_process().is_valid());

  RunAndVerify("/pkg/data/simple.tspec");

  // Running this test twice should work.
  // DX-448: Providers didn't properly reset themselves after a previous
  // trace was prematurely aborted.
  RunAndVerify("/pkg/data/simple.tspec");
}

// Provide our own main so that --verbose,etc. are recognized.
// This is useful because our verbosity is passed on to each test.
int main(int argc, char** argv)
{
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
