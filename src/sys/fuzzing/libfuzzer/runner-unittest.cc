// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/libfuzzer/runner.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <test/fuzzer/cpp/fidl.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/runner-unittest.h"
#include "src/sys/fuzzing/libfuzzer/testing/feedback.h"

namespace fuzzing {

using ::test::fuzzer::Relay;
using ::test::fuzzer::RelayPtr;
using ::test::fuzzer::SignaledBuffer;

// Test fixtures.

// libFuzzer's output is normally suppressed when testing; but can be enabled using this flag when
// debugging failed tests.
#define LIBFUZZER_SHOW_OUTPUT 0

// libFuzzer normally attaches to itself as a debugger to catch crashes; but can be prevented from
// doing so when another debugger like zxdb is needed to investigate failed tests.
#define LIBFUZZER_ALLOW_DEBUG 0

// Specializes the generic |RunnerTest| for |LibFuzzerRunner|.
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

  const RunnerPtr& runner() const override { return runner_; }

  void SetUp() override {
    RunnerTest::SetUp();
    runner_ = LibFuzzerRunner::MakePtr(executor());
    context_ = ComponentContext::CreateWithExecutor(executor());
    eventpair_ = std::make_unique<AsyncEventPair>(executor());
    ASSERT_EQ(test_input_vmo_.Reserve(kDefaultMaxInputSize), ZX_OK);
    ASSERT_EQ(feedback_vmo_.Mirror(&feedback_, sizeof(feedback_)), ZX_OK);
    // Convince libFuzzer that the code is instrumented.
    // See |Fuzzer::ReadAndExecuteSeedCorpora|.
    SetCoverage(Input("\n"), {{255, 255}});
  }

  void Configure(const OptionsPtr& options) override {
    // See notes on LIBFUZZER_ALLOW_DEBUG above.
    runner_->OverrideDefaults(options.get());
    options->set_debug(LIBFUZZER_ALLOW_DEBUG);
    RunnerTest::Configure(options);

    // See notes on LIBFUZZER_SHOW_OUTPUT above.
    auto libfuzzer_runner = std::static_pointer_cast<LibFuzzerRunner>(runner_);
    libfuzzer_runner->set_verbose(LIBFUZZER_SHOW_OUTPUT);

    // LibFuzzer's "entropic energy" feature allows it to focus on inputs that provide more useful
    // coverage; but is tricky to fake in unit testing.
    std::vector<std::string> cmdline{"bin/libfuzzer_unittest_fuzzer", "-entropic=0"};
    libfuzzer_runner->set_cmdline(std::move(cmdline));
  }

  ZxPromise<Input> GetTestInput() override {
    // Some workflows, notably |Cleanse|, may run multiple successful instances of the libFuzzer
    // process without error. This poses a challenge to this method, as it will be unclear whether
    // it is connecting to a running fuzzer or one that is in the process of exiting without error.
    // The easiest way to detect this is to simply wait for a fuzzing run to start without checking
    // if the fuzzer is connected. If it is not, or if it is exiting, then the wait will fail and
    // the test can connect to a new fuzzer instance via the relay. If it is, it is inexpensive to
    // simply wait again on the already active signal.
    return eventpair_->WaitFor(kStart)
        .and_then([](const zx_signals_t& observed) -> ZxResult<> { return fpromise::ok(); })
        .or_else([this, relay = RelayPtr(), connect = Future<>()](
                     Context& context, const zx_status_t& status) mutable -> ZxResult<> {
          // Connect to the fuzzer via the relay.
          if (!relay) {
            auto handler = context_->MakeRequestHandler<Relay>();
            handler(relay.NewRequest(executor()->dispatcher()));
          }
          if (!connect) {
            // Exchange shared objects with the fuzzer via the relay.
            SignaledBuffer data;
            data.eventpair = eventpair_->Create();
            if (auto status = test_input_vmo_.Share(&data.test_input); status != ZX_OK) {
              return fpromise::error(status);
            }
            if (auto status = feedback_vmo_.Share(&data.feedback); status != ZX_OK) {
              return fpromise::error(status);
            }
            Bridge<> bridge;
            relay->SetTestData(std::move(data), bridge.completer.bind());
            connect = bridge.consumer.promise_or(fpromise::error());
          }
          if (!connect(context)) {
            return fpromise::pending();
          }
          if (connect.is_error()) {
            return fpromise::error(ZX_ERR_PEER_CLOSED);
          }
          // At this point, the test should be connected to the fuzzer. Wait for a run to start.
          return fpromise::ok();
        })
        .and_then(eventpair_->WaitFor(kStart))
        .and_then([this](const zx_signals_t& observed) -> ZxResult<Input> {
          if (auto status = eventpair_->SignalSelf(kStart, 0); status != ZX_OK) {
            return fpromise::error(status);
          }
          auto input = Input(test_input_vmo_);
          return fpromise::ok(std::move(input));
        })
        .wrap_with(scope_);
  }

  ZxPromise<> SetFeedback(Coverage coverage, FuzzResult fuzz_result, bool leak) override {
    return fpromise::make_promise([this, coverage = std::move(coverage), fuzz_result, leak]() {
             feedback_.result = fuzz_result;
             feedback_.leak_suspected = leak;
             feedback_.num_counters = coverage.size();
             FX_DCHECK(feedback_.num_counters <= kMaxNumFeedbackCounters) << feedback_.num_counters;
             size_t i = 0;
             for (const auto& [offset, value] : coverage) {
               feedback_.counters[i].offset = static_cast<uint16_t>(offset);
               feedback_.counters[i].value = static_cast<uint8_t>(value);
               ++i;
             }
             feedback_vmo_.Update();
             return AsZxResult(eventpair_->SignalPeer(0, kStart));
           })
        .and_then(eventpair_->WaitFor(kFinish))
        .and_then([this](const zx_signals_t& observed) -> ZxResult<> {
          return AsZxResult(eventpair_->SignalSelf(kFinish, 0));
        })
        .or_else([](const zx_status_t& status) -> ZxResult<> {
          if (status == ZX_ERR_PEER_CLOSED) {
            // LibFuzzer oftens runs multiple fuzzers in child processes; don't treat exits as
            // failures.
            return fpromise::ok();
          }
          return fpromise::error(status);
        })
        .wrap_with(scope_);
  }

  void TearDown() override {
    // Clear temporary files.
    std::vector<std::string> paths;
    if (files::ReadDirContents("/tmp", &paths)) {
      for (const auto& path : paths) {
        files::DeletePath(files::JoinPath("/tmp", path), /* recursive */ true);
      }
    }
    RunnerTest::TearDown();
  }

 private:
  RunnerPtr runner_;
  std::unique_ptr<ComponentContext> context_;
  std::unique_ptr<AsyncEventPair> eventpair_;
  SharedMemory test_input_vmo_;
  SharedMemory feedback_vmo_;
  RelayedFeedback feedback_;
  Scope scope_;
};

#undef LIBFUZZER_SHOW_OUTPUT
#undef LIBFUZZER_ALLOW_DEBUG

#define RUNNER_TYPE LibFuzzerRunner
#define RUNNER_TEST LibFuzzerRunnerTest
#include "src/sys/fuzzing/common/runner-fatal-unittest.inc"
#include "src/sys/fuzzing/common/runner-unittest.inc"
#undef RUNNER_TYPE
#undef RUNNER_TEST

TEST_F(LibFuzzerRunnerTest, MergeSeedError) { MergeSeedError(/* expected */ ZX_OK, kOomLimit); }

TEST_F(LibFuzzerRunnerTest, Merge) { Merge(/* keep_errors= */ false, kOomLimit); }

}  // namespace fuzzing
