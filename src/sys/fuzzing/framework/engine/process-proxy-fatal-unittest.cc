// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"

namespace fuzzing {
namespace {

// Unit tests.
// Log detection is disabled for these tests, so they may emit fatal logs without failing.

using ProcessProxyFatalTest = ProcessProxyTest;

TEST_F(ProcessProxyFatalTest, Crash) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  process_proxy->SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  TestTarget target;
  auto process = target.Launch();
  process_proxy->Connect(IgnoreSentSignals(std::move(process)));
  target.Crash();
  EXPECT_EQ(process_proxy->GetResult(), FuzzResult::CRASH);
}

}  // namespace
}  // namespace fuzzing
