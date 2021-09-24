// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"

namespace fuzzing {

void ProcessProxyTest::SetUp() {
  pool_ = std::make_shared<ModulePool>();
}

ProcessProxySyncPtr ProcessProxyTest::Bind(ProcessProxyImpl* impl) {
  ProcessProxySyncPtr proxy;
  impl->Bind(proxy.NewRequest(), dispatcher_.get());
  return proxy;
}

zx::eventpair ProcessProxyTest::IgnoreSentSignals() {
  return coordinator_.Create([](zx_signals_t signals) { return true; });
}

zx::process ProcessProxyTest::IgnoreTarget() {
  return target_.Launch();
}

Options* ProcessProxyTest::IgnoreOptions() {
  return &ignored_;
}

void IgnoreReceivedSignals() {}

void IgnoreErrors(ProcessProxyImpl* ignored) {}

} // namespace fuzzing
