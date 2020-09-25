// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
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
#include "src/lib/fxl/test/test_settings.h"

// Defined in gtest_main.cc
extern syslog::LogSettings g_log_settings;

namespace tracing {
namespace test {

namespace {

// The URL of the basic integration test app (for fill-buffer, fill-buffer-and-alert, and simple).
const char kBasicIntegrationTestUrl[] =
    "fuchsia-pkg://fuchsia.com/trace_tests#meta/basic_integration_test_app.cmx";
// The URL of the nested environment integration test app.
const char kNestedEnvironmentTestUrl[] =
    "fuchsia-pkg://fuchsia.com/trace_tests#meta/nested_environment_test.cmx";
// The URL of the two-providers-two-engines integration test app.
const char kTwoProvidersTwoEnginesTestUrl[] =
    "fuchsia-pkg://fuchsia.com/trace_tests#meta/two_providers_two_engines_test_app.cmx";

void RunAndVerify(const std::string& app_path, const std::string& test_name,
                  const std::string& categories, size_t buffer_size_in_mb,
                  const std::string& buffering_mode,
                  std::initializer_list<std::string> additional_arguments = {}) {
  ASSERT_TRUE(RunIntegrationTest(app_path, test_name, categories, buffer_size_in_mb, buffering_mode,
                                 additional_arguments, kRelativeOutputFilePath, g_log_settings));
  ASSERT_TRUE(VerifyIntegrationTest(app_path, test_name, buffer_size_in_mb, buffering_mode,
                                    kRelativeOutputFilePath, g_log_settings));
}

TEST(Oneshot, FillBuffer) {
  RunAndVerify(kBasicIntegrationTestUrl, "fill-buffer", "trace:test", 1, "oneshot");
}

TEST(Circular, FillBuffer) {
  RunAndVerify(kBasicIntegrationTestUrl, "fill-buffer", "trace:test", 1, "circular");
}

TEST(CircularWithTrigger, FillBufferAndAlert) {
  RunAndVerify(kBasicIntegrationTestUrl, "fill-buffer-and-alert", "trace:test", 1, "circular",
               {"--trigger=alert:stop"});
}

TEST(Streaming, FillBuffer) {
  RunAndVerify(kBasicIntegrationTestUrl, "fill-buffer", "trace:test", 1, "streaming");
}

TEST(NestedTestEnvironment, Test) {
  RunAndVerify(kNestedEnvironmentTestUrl, "nested-environment-test", "trace:test", 1, "oneshot",
               {"--environment-name=environment_name"});
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
    AppendLoggingArgs(&argv_, "", g_log_settings);
    zx::eventpair their_event;
    auto status = zx::eventpair::create(0u, &our_event_, &their_event);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Error creating event pair: " << zx_status_get_string(status);
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
      FX_LOGS(ERROR) << "Failed waiting for provider process to start: "
                     << zx_status_get_string(status);
      TearDown();
    }
    FX_LOGS(INFO) << GetProgramPath() << " started";
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
      FX_LOGS(INFO) << GetProgramPath() << " terminated";
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

TEST_F(TwoProvidersOneEngine, ErrorHandling) {
  ASSERT_TRUE(provider_process().is_valid());

  RunAndVerify(kBasicIntegrationTestUrl, "simple", "trace:test", 1, "oneshot");

  // Running this test twice should work.
  // fxbug.dev/22912: Providers didn't properly reset themselves after a previous
  // trace was prematurely aborted.
  RunAndVerify(kBasicIntegrationTestUrl, "simple", "trace:test", 1, "oneshot");
}

TEST(TwoProvidersTwoEngines, Test) {
  RunAndVerify(kTwoProvidersTwoEnginesTestUrl, "two-providers-two-engines", "trace:test", 1,
               "oneshot");
}

}  // namespace

}  // namespace test
}  // namespace tracing
