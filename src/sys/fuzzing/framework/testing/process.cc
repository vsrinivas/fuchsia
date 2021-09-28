// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/process.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Feedback;

fidl::InterfaceRequest<ProcessProxy> FakeProcess::NewRequest() { return proxy_.NewRequest(); }

void FakeProcess::Connect() {
  FX_DCHECK(proxy_);
  auto eventpair = coordinator_.Create([&](zx_signals_t observed) {
    switch (observed) {
      case kStart:
      case kStartLeakCheck:
        module_.Clear();
        coordinator_.SignalPeer(kStart);
        return true;
      case kFinish:
        module_.Update();
        coordinator_.SignalPeer(leak_suspected_ ? kFinishWithLeaks : kFinish);
        leak_suspected_ = false;
        return true;
      default:
        return false;
    }
  });
  Options ignored;
  proxy_->Connect(std::move(eventpair), target_.Launch(), &ignored);
}

void FakeProcess::AddFeedback() {
  FX_DCHECK(proxy_);
  Feedback feedback;
  feedback.set_id(module_.id());
  feedback.set_inline_8bit_counters(module_.Share());
  proxy_->AddFeedback(std::move(feedback));
}

void FakeProcess::SetLeak(bool leak_suspected) { leak_suspected_ = leak_suspected; }

void FakeProcess::SetCoverage(const Coverage& coverage) { module_.SetCoverage(coverage); }

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
  proxy_ = nullptr;
  coordinator_.Reset();
}

}  // namespace fuzzing
