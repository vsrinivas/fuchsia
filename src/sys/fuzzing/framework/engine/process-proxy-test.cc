// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"

#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

void ProcessProxyTest::SetUp() {
  AsyncTest::SetUp();
  pool_ = std::make_shared<ModulePool>();
  process_ = std::make_unique<FakeProcess>(executor());
}

std::unique_ptr<ProcessProxy> ProcessProxyTest::MakeProcessProxy() {
  return std::make_unique<ProcessProxy>(executor(), kInvalidTargetId + 1, pool_);
}

OptionsPtr ProcessProxyTest::DefaultOptions() {
  auto options = MakeOptions();
  ProcessProxy::AddDefaults(options.get());
  return options;
}

InstrumentedProcess ProcessProxyTest::IgnoreSentSignals(zx::process&& process) {
  return process_->IgnoreSentSignals(std::move(process));
}

InstrumentedProcess ProcessProxyTest::IgnoreTarget(zx::eventpair&& eventpair) {
  return process_->IgnoreTarget(std::move(eventpair));
}

InstrumentedProcess ProcessProxyTest::IgnoreAll() { return process_->IgnoreAll(); }

void IgnoreReceivedSignals() {}

void IgnoreErrors(uint64_t ignored) {}

}  // namespace fuzzing
