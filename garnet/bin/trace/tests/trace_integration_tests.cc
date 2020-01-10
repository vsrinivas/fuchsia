// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "garnet/bin/trace/tests/component_context.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"
#include "src/developer/tracing/lib/test_utils/run_program.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"

namespace tracing {
namespace test {

namespace {

// |relative_tspec_path| is a relative path, from /pkg.
static void RunAndVerify(const char* relative_tspec_path) {
  ASSERT_TRUE(RunTspec(relative_tspec_path, kRelativeOutputFilePath));
  ASSERT_TRUE(VerifyTspec(relative_tspec_path, kRelativeOutputFilePath));
}

TEST(Oneshot, FillBuffer) { RunAndVerify("data/oneshot.tspec"); }

TEST(Circular, FillBuffer) { RunAndVerify("data/circular.tspec"); }

TEST(Streaming, FillBuffer) { RunAndVerify("data/streaming.tspec"); }

TEST(NestedTestEnvironment, Test) {
  ASSERT_TRUE(RunTspec("data/nested_environment_test.tspec", kRelativeOutputFilePath));
}

// A class for adding an extra provider to the test.

class ExtraProvider : public ::testing::Test {
 protected:
  // Path of the program that starts two providers using one engine.
  virtual const char* GetProgramPath() = 0;

  const zx::process& provider_process() const { return provider_process_; }

  void SetUp() override {
    zx::job job{};  // -> default job
    argv_.push_back(GetProgramPath());
    AppendLoggingArgs(&argv_, "");
    zx::eventpair their_event;
    auto status = zx::eventpair::create(0u, &our_event_, &their_event);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error creating event pair: " << zx_status_get_string(status);
      return;
    }
    status = SpawnProgram(job, argv_, their_event.release(), &provider_process_);
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
    // Leave it to the test harness to provide a timeout. If it doesn't that's
    // its bug.
    status = our_event_.wait_many(&wait_items[0], 2, zx::time::infinite());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed waiting for provider process to start: "
                     << zx_status_get_string(status);
      TearDown();
    }
    FXL_LOG(INFO) << GetProgramPath() << " started";
  }

  void TearDown() override {
    if (provider_process_.is_valid()) {
      our_event_.reset();
      int64_t return_code;
      bool wait_success = WaitAndGetReturnCode(argv_[0], provider_process_, &return_code);
      EXPECT_TRUE(wait_success);
      if (wait_success) {
        EXPECT_EQ(return_code, 0);
      }
      provider_process_.reset();
      FXL_LOG(INFO) << GetProgramPath() << " terminated";
    }
  }

 private:
  zx::eventpair our_event_;
  zx::process provider_process_;
  std::vector<std::string> argv_;
};

// We support two providers in one process, but it's the process's
// responsibility to get it right. E.g., Two providers using one trace-engine
// is a non-starter.
class TwoProvidersOneEngine : public ExtraProvider {
 public:
  const char* GetProgramPath() override { return "/pkg/bin/two_providers_one_engine"; }
};

// TODO(42621): Disabled due to flake.
TEST_F(TwoProvidersOneEngine, DISABLED_ErrorHandling) {
  ASSERT_TRUE(provider_process().is_valid());

  RunAndVerify("data/simple.tspec");

  // Running this test twice should work.
  // DX-448: Providers didn't properly reset themselves after a previous
  // trace was prematurely aborted.
  RunAndVerify("data/simple.tspec");
}

TEST(TwoProvidersTwoEngines, DISABLED_Test) {
  RunAndVerify("data/two_providers_two_engines.tspec");
}

}  // namespace

}  // namespace test
}  // namespace tracing
