// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/process.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;

FakeProcess::FakeProcess(ExecutorPtr executor)
    : executor_(executor), eventpair_(executor), target_(executor) {}

ZxPromise<> FakeProcess::Launch() {
  FX_DCHECK(handler_);
  return fpromise::make_promise(
             [this, instrumentation = InstrumentationPtr(), add_process = ZxFuture<zx_signals_t>(),
              add_module = ZxFuture<zx_signals_t>()](Context& context) mutable -> ZxResult<> {
               if (running_) {
                 return fpromise::ok();
               }
               if (!instrumentation) {
                 handler_(instrumentation.NewRequest(executor_->dispatcher()));
               }
               if (!add_process) {
                 InstrumentedProcess instrumented;
                 instrumented.set_eventpair(eventpair_.Create());
                 instrumented.set_process(target_.Launch());
                 Bridge<Options> bridge;
                 instrumentation->Initialize(std::move(instrumented), bridge.completer.bind());
                 add_process = bridge.consumer.promise_or(fpromise::error())
                                   .or_else([] { return fpromise::error(ZX_ERR_CANCELED); })
                                   .and_then([](const Options& options) { return fpromise::ok(); })
                                   .and_then(eventpair_.WaitFor(kSync));
               }
               if (!add_process(context)) {
                 return fpromise::pending();
               }
               if (add_process.is_error()) {
                 return fpromise::error(add_process.error());
               }
               if (!add_module) {
                 Bridge<> bridge;
                 instrumentation->AddLlvmModule(module_.GetLlvmModule(), bridge.completer.bind());
                 add_module = bridge.consumer.promise_or(fpromise::error())
                                  .or_else([] { return fpromise::error(ZX_ERR_CANCELED); })
                                  .and_then(eventpair_.WaitFor(kSync));
               }
               if (!add_module(context)) {
                 return fpromise::pending();
               }
               if (add_module.is_error()) {
                 return fpromise::error(add_module.error());
               }
               executor_->schedule_task(AwaitStart());
               running_ = true;
               return fpromise::ok();
             })
      .wrap_with(scope_);
}

void FakeProcess::SetLeak(bool leak_suspected) { leak_suspected_ = leak_suspected; }

void FakeProcess::SetCoverage(const Coverage& coverage) { module_.SetCoverage(coverage); }

InstrumentedProcess FakeProcess::IgnoreSentSignals(zx::process&& process) {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(eventpair_.Create());
  instrumented.set_process(std::move(process));
  return instrumented;
}

InstrumentedProcess FakeProcess::IgnoreTarget(zx::eventpair&& eventpair) {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(std::move(eventpair));
  instrumented.set_process(target_.Launch());
  return instrumented;
}

InstrumentedProcess FakeProcess::IgnoreAll() {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(eventpair_.Create());
  instrumented.set_process(target_.Launch());
  return instrumented;
}

ZxPromise<> FakeProcess::AwaitStart() {
  return fpromise::make_promise(
             [this, start = ZxFuture<zx_signals_t>()](Context& context) mutable -> ZxResult<> {
               while (true) {
                 if (!start) {
                   start = eventpair_.WaitFor(kStart | kStartLeakCheck);
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
                 auto status = eventpair_.SignalSelf(start.take_value(), 0);
                 if (status != ZX_OK) {
                   return fpromise::error(status);
                 }
                 status = eventpair_.SignalPeer(0, kStart);
                 if (status != ZX_OK) {
                   return fpromise::error(status);
                 }
               }
             })
      .wrap_with(scope_);
}

ZxPromise<> FakeProcess::AwaitFinish() {
  return eventpair_.WaitFor(kFinish)
      .and_then([this](const zx_signals_t& observed) -> ZxResult<> {
        module_.Update();
        auto status = eventpair_.SignalSelf(observed, 0);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        status = eventpair_.SignalPeer(0, leak_suspected_ ? kFinishWithLeaks : kFinish);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> FakeProcess::ExitAsync(int32_t exitcode) {
  return target_.Exit(exitcode)
      .and_then([this] {
        eventpair_.Reset();
        running_ = false;
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

ZxPromise<> FakeProcess::CrashAsync() {
  return target_.Crash()
      .and_then([this] {
        eventpair_.Reset();
        running_ = false;
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

}  // namespace fuzzing
