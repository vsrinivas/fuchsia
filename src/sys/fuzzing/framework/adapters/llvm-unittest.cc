// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <lib/sync/completion.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/testing/signal-coordinator.h"

// Test fixtures.

static struct {
  const char* data;
  size_t size;
} last_input;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  last_input.data = reinterpret_cast<const char*>(data);
  last_input.size = size;
  return 0;
}

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::TargetAdapterSyncPtr;

// Unit tests.

TEST(LLVMTargetAdapterTest, Connect) {
  auto dispatcher = std::make_shared<Dispatcher>();
  LLVMTargetAdapter adapter(dispatcher);

  sync_completion_t closed;
  auto handler = adapter.GetHandler([&]() { /* on_close */ sync_completion_signal(&closed); });

  TargetAdapterSyncPtr ptr;
  handler(ptr.NewRequest());

  FakeSignalCoordinator coordinator;

  SharedMemory test_input;
  test_input.Reserve(1 << 12);

  ptr->Connect(coordinator.Create(), test_input.Share());

  // Test sending and signalling a test_input.
  std::string s("hello world!");
  memset(&last_input, 0, sizeof(last_input));
  test_input.Write(s.data(), s.size() + 1);
  EXPECT_EQ(last_input.data, nullptr);
  EXPECT_EQ(last_input.size, 0U);

  EXPECT_TRUE(coordinator.SignalPeer(Signal::kStart));
  EXPECT_EQ(coordinator.AwaitSignal(), Signal::kFinish);
  EXPECT_STREQ(last_input.data, s.data());
  EXPECT_EQ(last_input.size, test_input.size());

  coordinator.Reset();
  sync_completion_wait(&closed, ZX_TIME_INFINITE);
}

}  // namespace
}  // namespace fuzzing
