// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy.h"
#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {
namespace {

// Unit tests.
// Log detection is disabled for these tests, so they may emit fatal logs without failing.

using ProcessProxyFatalTest = ProcessProxyTest;

TEST_F(ProcessProxyFatalTest, Crash) {
  ProcessProxyImpl impl(pool());
  impl.Configure(DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  auto process = target.Launch();
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), std::move(process), IgnoreOptions()), ZX_OK);
  target.Crash();
  EXPECT_EQ(impl.Join(), Result::CRASH);
}

} // namespace
} // namespace fuzzing
