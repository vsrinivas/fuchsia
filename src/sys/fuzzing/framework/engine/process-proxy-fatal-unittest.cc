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
  TestTarget target(executor());
  auto process = target.Launch();
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(std::move(process))), ZX_OK);
  FUZZING_EXPECT_OK(target.Crash());
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::CRASH);
  RunUntilIdle();
}

}  // namespace
}  // namespace fuzzing
