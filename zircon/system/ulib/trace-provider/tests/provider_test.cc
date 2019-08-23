// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>
#include <zxtest/zxtest.h>

#include "fake_trace_manager.h"

namespace trace {
namespace {

// Test handling of early loop cancel by having the loop be destructed before the provider.
TEST(ProviderTest, EarlyLoopCancel) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::unique_ptr<trace::test::FakeTraceManager> manager;
  zx::channel channel;
  trace::test::FakeTraceManager::Create(loop.dispatcher(), &manager, &channel);

  TraceProvider provider{std::move(channel), loop.dispatcher()};
  loop.RunUntilIdle();
  loop.Shutdown();
}

}  // namespace
}  // namespace trace
