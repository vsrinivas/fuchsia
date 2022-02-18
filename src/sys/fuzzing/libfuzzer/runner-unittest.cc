// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/runner.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <test/fuzzer/cpp/fidl.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/runner-unittest.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/common/testing/signal-coordinator.h"
#include "src/sys/fuzzing/libfuzzer/testing/feedback.h"

namespace fuzzing {

using ::test::fuzzer::RelaySyncPtr;
using ::test::fuzzer::SignaledBuffer;

// Test fixtures.

// libFuzzer's output is normally suppressed when testing; but can be enabled using this flag when
// debugging failed tests.
#define LIBFUZZER_SHOW_OUTPUT 0

// libFuzzer normally attaches to itself as a debugger to catch crashes; but can be prevented from
// doing so when another debugger like zxdb is needed to investigate failed tests.
#define LIBFUZZER_ALLOW_DEBUG 0

class LibFuzzerRunnerTest : public RunnerTest {
 protected:
  //////////////////////////////////////
  // Test fixtures.

  // Some of libFuzzer's workflows spawn "inner" processes that test actual inputs and may fault,
  // while the original, "outer" process controls their execution and should be fault-resistant. If
  // the OOM limit is set to low, these "outer" processes may fault as well. This is especially
  // noticeable when running with ASan, where the outer process has been observed to use 35 MB of
  // memory or more.
  static const uint64_t kOomLimit = 1ULL << 26;  // 64 MB

  void SetUp() override {
    test_input_buffer_.Reserve(kDefaultMaxInputSize);
    feedback_buffer_.Mirror(&feedback_, sizeof(feedback_));
    // Convince libFuzzer that the code is instrumented.
    // See |Fuzzer::ReadAndExecuteSeedCorpora|.
    SetCoverage(Input("\n"), {{255, 255}});
  }

  void Configure(Runner* runner, const std::shared_ptr<Options>& options) override {
    RunnerTest::Configure(runner, options);
    auto* testrunner = static_cast<LibFuzzerRunner*>(runner);
    std::vector<std::string> cmdline{"/pkg/bin/libfuzzer_test_fuzzer"};

// See notes on LIBFUZZER_SHOW_OUTPUT above.
#if LIBFUZZER_SHOW_OUTPUT
    testrunner->set_verbose(true);
#else
    testrunner->set_verbose(false);
#endif  // LIBFUZZER_SHOW_OUTPUT

// See notes on LIBFUZZER_ALLOW_DEBUG above.
#if LIBFUZZER_ALLOW_DEBUG
    cmdline.push_back("-handle_segv=0");
    cmdline.push_back("-handle_bus=0");
    cmdline.push_back("-handle_ill=0");
    cmdline.push_back("-handle_fpe=0");
    cmdline.push_back("-handle_abrt=0");
#endif  // LIBFUZZER_ALLOW_DEBUG

    testrunner->set_cmdline(std::move(cmdline));
  }

  bool HasTestInput(zx::time deadline) override {
    if (has_test_input_) {
      return true;
    }
    if (!coordinator_.is_valid()) {
      RelaySyncPtr relay;
      auto context = sys::ComponentContext::Create();
      auto status = context->svc()->Connect(relay.NewRequest());
      FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
      SignaledBuffer data;
      data.eventpair = coordinator_.Create();
      data.test_input = test_input_buffer_.Share();
      data.feedback = feedback_buffer_.Share();
      status = relay->SetTestData(std::move(data));
      FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    }
    zx_signals_t observed;
    has_test_input_ =
        coordinator_.AwaitSignal(deadline, &observed) == ZX_OK && (observed & kStart) != 0;
    return has_test_input_;
  }

  Input GetTestInput() override {
    has_test_input_ = false;
    return Input(test_input_buffer_);
  }

  void SetFeedback(const Coverage& coverage, FuzzResult result, bool leak) override {
    feedback_.result = result;
    feedback_.leak_suspected = leak;
    feedback_.num_counters = coverage.size();
    size_t i = 0;
    for (const auto& offset_value : coverage) {
      feedback_.counters[i].offset = static_cast<uint16_t>(offset_value.first);
      feedback_.counters[i].value = static_cast<uint8_t>(offset_value.second);
      ++i;
    }
    feedback_buffer_.Update();
    has_test_input_ = coordinator_.SignalPeer(kStart) && (coordinator_.AwaitSignal() & kStart) != 0;
  }

  void TearDown() override {
    // Clear temporary files.
    std::vector<std::string> paths;
    if (files::ReadDirContents("/tmp", &paths)) {
      for (const auto& path : paths) {
        files::DeletePath(files::JoinPath("/tmp", path), /* recursive */ true);
      }
    }
  }

 private:
  FakeSignalCoordinator coordinator_;
  bool has_test_input_ = false;
  SharedMemory test_input_buffer_;
  SharedMemory feedback_buffer_;
  RelayedFeedback feedback_;
};

#undef LIBFUZZER_SHOW_OUTPUT
#undef LIBFUZZER_ALLOW_DEBUG

#define RUNNER_TYPE LibFuzzerRunner
#define RUNNER_TEST LibFuzzerRunnerTest
#include "src/sys/fuzzing/common/runner-fatal-unittest.inc"
#include "src/sys/fuzzing/common/runner-unittest.inc"
#undef RUNNER_TYPE
#undef RUNNER_TEST

TEST_F(LibFuzzerRunnerTest, MergeSeedError) {
  LibFuzzerRunner runner;
  MergeSeedError(&runner, /* expected */ ZX_OK, kOomLimit);
}

TEST_F(LibFuzzerRunnerTest, Merge) {
  LibFuzzerRunner runner;
  Merge(&runner, /* keep_errors= */ false, kOomLimit);
}

}  // namespace fuzzing
