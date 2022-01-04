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

fidl::InterfaceRequest<Instrumentation> FakeProcess::NewRequest() {
  return instrumentation_.NewRequest();
}

void FakeProcess::AddProcess() {
  FX_DCHECK(instrumentation_);
  auto eventpair = coordinator_.Create([&](zx_signals_t observed) {
    switch (observed) {
      case kSync:
        sync_.Signal();
        return true;
      case kStart:
      case kStartLeakCheck:
        module_.Clear();
        leak_suspected_ = false;
        return coordinator_.SignalPeer(kStart);
      case kFinish:
        module_.Update();
        return coordinator_.SignalPeer(leak_suspected_ ? kFinishWithLeaks : kFinish);
      default:
        return false;
    }
  });
  Options ignored;
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(std::move(eventpair));
  instrumented.set_process(target_.Launch());
  sync_.Reset();
  instrumentation_->Initialize(std::move(instrumented), &ignored);
  sync_.WaitFor("sync signal from Initialize");
}

void FakeProcess::AddLlvmModule() {
  FX_DCHECK(instrumentation_);
  sync_.Reset();
  instrumentation_->AddLlvmModule(module_.GetLlvmModule());
  sync_.WaitFor("sync signal from AddLlvmModule");
}

void FakeProcess::SetLeak(bool leak_suspected) { leak_suspected_ = leak_suspected; }

void FakeProcess::SetCoverage(const Coverage& coverage) { module_.SetCoverage(coverage); }

zx::eventpair FakeProcess::MakeIgnoredEventpair() {
  return coordinator_.Create([](zx_signals_t signals) { return true; });
}

zx::process FakeProcess::MakeIgnoredProcess() { return target_.Launch(); }

InstrumentedProcess FakeProcess::IgnoreSentSignals(zx::process&& process) {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(MakeIgnoredEventpair());
  instrumented.set_process(std::move(process));
  return instrumented;
}

InstrumentedProcess FakeProcess::IgnoreTarget(zx::eventpair&& eventpair) {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(std::move(eventpair));
  instrumented.set_process(MakeIgnoredProcess());
  return instrumented;
}

InstrumentedProcess FakeProcess::IgnoreAll() {
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(MakeIgnoredEventpair());
  instrumented.set_process(MakeIgnoredProcess());
  return instrumented;
}

void FakeProcess::Exit(int32_t exitcode) {
  target_.Exit(exitcode);
  target_.Join();
  Reset();
}

void FakeProcess::Crash() {
  target_.Crash();
  target_.Join();
  Reset();
}

void FakeProcess::Reset() {
  instrumentation_ = nullptr;
  coordinator_.Reset();
}

}  // namespace fuzzing
