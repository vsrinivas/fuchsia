// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"

#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

void ProcessProxyTest::SetUp() {
  dispatcher_ = std::make_shared<Dispatcher>();
  pool_ = std::make_shared<ModulePool>();
}

std::unique_ptr<ProcessProxyImpl> ProcessProxyTest::MakeProcessProxy() {
  return std::make_unique<ProcessProxyImpl>(kInvalidTargetId + 1, pool_);
}

std::shared_ptr<Options> ProcessProxyTest::DefaultOptions() {
  auto options = std::make_shared<Options>();
  ProcessProxyImpl::AddDefaults(options.get());
  return options;
}

InstrumentedProcess ProcessProxyTest::IgnoreSentSignals(zx::process&& process) {
  return process_.IgnoreSentSignals(std::move(process));
}

InstrumentedProcess ProcessProxyTest::IgnoreTarget(zx::eventpair&& eventpair) {
  return process_.IgnoreTarget(std::move(eventpair));
}

InstrumentedProcess ProcessProxyTest::IgnoreAll() { return process_.IgnoreAll(); }

void IgnoreReceivedSignals() {}

void IgnoreErrors(uint64_t ignored) {}

void ProcessProxyTest::TearDown() { dispatcher_->Shutdown(); }

}  // namespace fuzzing
