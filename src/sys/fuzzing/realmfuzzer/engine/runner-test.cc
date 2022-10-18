// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/runner-test.h"

#include <lib/fidl/cpp/interface_handle.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"

namespace fuzzing {

void RealmFuzzerRunnerTest::SetUp() {
  RunnerTest::SetUp();
  runner_ = RealmFuzzerRunner::MakePtr(executor());
  auto runner_impl = std::static_pointer_cast<RealmFuzzerRunner>(runner_);
  target_adapter_ = std::make_unique<FakeTargetAdapter>(executor());
  runner_impl->SetTargetAdapterHandler(target_adapter_->GetHandler());

  coverage_ = std::make_unique<FakeCoverage>(executor());
  auto handler = coverage_->GetProviderHandler();
  fidl::InterfaceHandle<CoverageDataProvider> provider;
  handler(provider.NewRequest());
  EXPECT_EQ(runner_impl->BindCoverageDataProvider(provider.TakeChannel()), ZX_OK);

  eventpair_ = std::make_unique<AsyncEventPair>(executor());
}

void RealmFuzzerRunnerTest::SetAdapterParameters(const std::vector<std::string>& parameters) {
  target_adapter_->SetParameters(parameters);
}

ZxPromise<Input> RealmFuzzerRunnerTest::GetTestInput() {
  auto stash = std::make_shared<Input>();
  return target_adapter_->AwaitStart()
      .and_then([this, stash](Input& input) -> ZxResult<> {
        *stash = std::move(input);
        if (target_) {
          return fpromise::error(ZX_ERR_ALREADY_EXISTS);
        }
        target_ = std::make_unique<TestTarget>(executor());
        return fpromise::ok();
      })
      .and_then(PublishProcess())
      .and_then(PublishModule())
      .or_else([](const zx_status_t& status) -> ZxResult<> {
        if (status != ZX_ERR_ALREADY_EXISTS) {
          return fpromise::error(status);
        }
        return fpromise::ok();
      })
      .and_then([stash] { return fpromise::ok(std::move(*stash)); })
      .wrap_with(scope_);
}

ZxPromise<> RealmFuzzerRunnerTest::PublishProcess() {
  Bridge<Options> bridge;
  return fpromise::make_promise(
             [this, completer = std::move(bridge.completer)]() mutable -> ZxResult<> {
               // Connect and send the process.
               auto handler = coverage_->GetCollectorHandler();
               handler(collector_.NewRequest(executor()->dispatcher()));
               InstrumentedProcess instrumented{
                   .eventpair = eventpair_->Create(),
                   .process = target_->Launch(),
               };
               collector_->Initialize(std::move(instrumented), completer.bind());
               return fpromise::ok();
             })
      .and_then([wait = Future<Options>(bridge.consumer.promise_or(fpromise::error()))](
                    Context& context) mutable -> ZxResult<> {
        if (!wait(context)) {
          return fpromise::pending();
        }
        if (wait.is_error()) {
          return fpromise::error(ZX_ERR_CANCELED);
        }
        return fpromise::ok();
      });
}

ZxPromise<> RealmFuzzerRunnerTest::PublishModule() {
  Bridge<> bridge;
  return fpromise::make_promise([this,
                                 completer = std::move(bridge.completer)]() mutable -> ZxResult<> {
           zx::vmo inline_8bit_counters;
           if (auto status = module_.Share(target_->id(), &inline_8bit_counters); status != ZX_OK) {
             return fpromise::error(status);
           }
           collector_->AddLlvmModule(std::move(inline_8bit_counters), completer.bind());
           return fpromise::ok();
         })
      .and_then([this, wait = Future<>(bridge.consumer.promise_or(fpromise::error()))](
                    Context& context) mutable -> ZxResult<> {
        if (!wait(context)) {
          return fpromise::pending();
        }
        if (wait.is_error()) {
          return fpromise::error(ZX_ERR_CANCELED);
        }
        // Automatically clear feedback on start. This will complete when |eventpair_| is reset.
        executor()->schedule_task(AwaitStart());
        return fpromise::ok();
      });
}

ZxPromise<> RealmFuzzerRunnerTest::SetFeedback(Coverage coverage, FuzzResult fuzz_result,
                                               bool leak) {
  return fpromise::make_promise(
             [this, coverage = std::move(coverage), leak, fuzz_result]() mutable -> ZxResult<> {
               if (fuzz_result != FuzzResult::NO_ERRORS) {
                 return fpromise::ok();
               }
               // Fake some activity within the process.
               module_.SetCoverage(coverage);
               leak_suspected_ = leak;
               return AsZxResult(target_adapter_->Finish());
             })
      .and_then([this, fuzz_result, target = ZxFuture<>(),
                 disconnect = ZxFuture<>()](Context& context) mutable -> ZxResult<> {
        if (!target) {
          switch (fuzz_result) {
            case FuzzResult::NO_ERRORS:
              target = AwaitFinish();
              break;
            case FuzzResult::BAD_MALLOC:
              target = ExitAsync(options()->malloc_exitcode());
              break;
            case FuzzResult::CRASH:
              target = CrashAsync();
              break;
            case FuzzResult::DEATH:
              target = ExitAsync(options()->death_exitcode());
              break;
            case FuzzResult::EXIT:
              target = ExitAsync(1);
              break;
            case FuzzResult::LEAK:
              target = ExitAsync(options()->leak_exitcode());
              break;
            case FuzzResult::OOM:
              target = ExitAsync(options()->oom_exitcode());
              break;
            case FuzzResult::TIMEOUT:
              // Don't signal from the target adapter and don't exit the fake process; just wait.
              // Eventually, the runner's will time out and kill the process.
              target = executor()->MakePromiseForTime(zx::time::infinite()).or_else([] {
                return fpromise::error(ZX_ERR_TIMED_OUT);
              });
              break;
          }
        }
        if (!target(context)) {
          return fpromise::pending();
        }
        if (target.is_error()) {
          return fpromise::error(target.error());
        }
        if (fuzz_result == FuzzResult::NO_ERRORS) {
          return fpromise::ok();
        }
        if (fuzz_result == FuzzResult::EXIT && !options()->detect_exits()) {
          return fpromise::ok();
        }
        // In most cases, the fake process stops, and unless the error is recoverable the target
        // adapter should, too.
        if (!disconnect) {
          disconnect = target_adapter_->AwaitDisconnect();
        }
        if (!disconnect(context)) {
          return fpromise::pending();
        }
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> RealmFuzzerRunnerTest::AwaitStart() {
  return fpromise::make_promise(
             [this, start = ZxFuture<zx_signals_t>()](Context& context) mutable -> ZxResult<> {
               while (true) {
                 if (!start) {
                   start = eventpair_->WaitFor(kStart | kStartLeakCheck);
                 }
                 if (!start(context)) {
                   return fpromise::pending();
                 }
                 if (start.is_error()) {
                   // Disconnected; stop waiting for start signals.
                   return fpromise::ok();
                 }
                 module_.Clear();
                 leak_suspected_ = false;
                 auto status = eventpair_->SignalSelf(start.take_value(), 0);
                 if (status != ZX_OK) {
                   return fpromise::error(status);
                 }
                 status = eventpair_->SignalPeer(0, kStart);
                 if (status != ZX_OK) {
                   return fpromise::error(status);
                 }
               }
             })
      .wrap_with(scope_);
}

ZxPromise<> RealmFuzzerRunnerTest::AwaitFinish() {
  return eventpair_->WaitFor(kFinish)
      .and_then([this](const zx_signals_t& observed) -> ZxResult<> {
        module_.Update();
        auto status = eventpair_->SignalSelf(observed, 0);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        status = eventpair_->SignalPeer(0, leak_suspected_ ? kFinishWithLeaks : kFinish);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> RealmFuzzerRunnerTest::ExitAsync(int32_t exitcode) {
  return target_->Exit(exitcode)
      .and_then([this] {
        eventpair_->Reset();
        target_.reset();
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> RealmFuzzerRunnerTest::CrashAsync() {
  return target_->Crash()
      .and_then([this] {
        eventpair_->Reset();
        target_.reset();
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

void RealmFuzzerRunnerTest::TearDown() {
  if (target_) {
    Schedule(ExitAsync(0));
    RunUntilIdle();
  }
  RunnerTest::TearDown();
}

}  // namespace fuzzing
