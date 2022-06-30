// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner-test.h"

#include <lib/fidl/cpp/interface_handle.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/coverage-data.h"

namespace fuzzing {

using fuchsia::fuzzer::Payload;

void RunnerImplTest::SetUp() {
  RunnerTest::SetUp();
  runner_ = RunnerImpl::MakePtr(executor());
  auto runner_impl = std::static_pointer_cast<RunnerImpl>(runner_);

  target_adapter_ = std::make_unique<FakeTargetAdapter>(executor());
  runner_impl->set_target_adapter_handler(target_adapter_->GetHandler());

  events_ = AsyncDeque<CoverageEvent>::MakePtr();
  coverage_provider_ = std::make_unique<CoverageProviderImpl>(executor(), options(), events_);
  runner_impl->set_coverage_provider_handler(coverage_provider_->GetHandler());

  eventpair_ = std::make_unique<AsyncEventPair>(executor());
  target_ = std::make_unique<TestTarget>(executor());
}

void RunnerImplTest::SetAdapterParameters(const std::vector<std::string>& parameters) {
  target_adapter_->SetParameters(parameters);
}

ZxPromise<Input> RunnerImplTest::GetTestInput() {
  auto stash = std::make_shared<Input>();
  return target_adapter_->AwaitStart()
      .and_then([this, stash](Input& input) -> ZxResult<> {
        *stash = std::move(input);
        if (running_) {
          return fpromise::error(ZX_ERR_ALREADY_EXISTS);
        }
        running_ = true;
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

ZxPromise<> RunnerImplTest::PublishProcess() {
  return fpromise::make_promise(
      [this, wait = ZxFuture<zx_signals_t>()](Context& context) mutable -> ZxResult<> {
        if (!wait) {
          auto process = target_->Launch();
          zx_info_handle_basic_t info;
          if (auto status =
                  process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
              status != ZX_OK) {
            return fpromise::error(status);
          }
          target_id_ = info.koid;
          InstrumentedProcess instrumented;
          instrumented.set_process(std::move(process));
          instrumented.set_eventpair(eventpair_->Create());
          CoverageEvent event;
          event.target_id = target_id_;
          event.payload = Payload::WithProcessStarted(std::move(instrumented));
          if (auto status = events_->Send(std::move(event)); status != ZX_OK) {
            return fpromise::error(status);
          }
          wait = eventpair_->WaitFor(kSync);
        }
        if (!wait(context)) {
          return fpromise::pending();
        }
        if (wait.is_error()) {
          return fpromise::error(wait.error());
        }
        return fpromise::ok();
      });
}

ZxPromise<> RunnerImplTest::PublishModule() {
  return fpromise::make_promise(
      [this, wait = ZxFuture<zx_signals_t>()](Context& context) mutable -> ZxResult<> {
        if (!wait) {
          LlvmModule llvm_module;
          llvm_module.set_legacy_id(module_.legacy_id());
          zx::vmo inline_8bit_counters;
          EXPECT_EQ(module_.Share(target_id_, &inline_8bit_counters), ZX_OK);
          llvm_module.set_inline_8bit_counters(std::move(inline_8bit_counters));
          CoverageEvent event;
          event.target_id = target_id_;
          event.payload = Payload::WithLlvmModuleAdded(std::move(llvm_module));
          if (auto status = events_->Send(std::move(event)); status != ZX_OK) {
            return fpromise::error(status);
          }
          wait = eventpair_->WaitFor(kSync);
        }
        if (!wait(context)) {
          return fpromise::pending();
        }
        if (wait.is_error()) {
          return fpromise::error(wait.error());
        }
        // Automatically clear feedback on start. This will complete when |eventpair_| is reset.
        executor()->schedule_task(AwaitStart());
        return fpromise::ok();
      });
}

ZxPromise<> RunnerImplTest::SetFeedback(Coverage coverage, FuzzResult fuzz_result, bool leak) {
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
          running_ = true;
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
        running_ = false;
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> RunnerImplTest::AwaitStart() {
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

ZxPromise<> RunnerImplTest::AwaitFinish() {
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

ZxPromise<> RunnerImplTest::ExitAsync(int32_t exitcode) {
  return target_->Exit(exitcode)
      .and_then([this] {
        eventpair_->Reset();
        running_ = false;
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> RunnerImplTest::CrashAsync() {
  return target_->Crash()
      .and_then([this] {
        eventpair_->Reset();
        running_ = false;
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

}  // namespace fuzzing
