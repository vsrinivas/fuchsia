// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/sync-wait.h"
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

TargetAdapterSyncPtr MakeSyncPtr(LLVMTargetAdapter& adapter) {
  TargetAdapterSyncPtr ptr;
  auto handler = adapter.GetHandler();
  handler(ptr.NewRequest());
  return ptr;
}

// Unit tests.

TEST(LLVMTargetAdapterTest, GetParameters) {
  LLVMTargetAdapter adapter;
  std::vector<std::string> sent({"foo", "bar", "baz"});
  adapter.SetParameters(sent);

  auto ptr = MakeSyncPtr(adapter);
  std::vector<std::string> received;
  EXPECT_EQ(ptr->GetParameters(&received), ZX_OK);
  EXPECT_EQ(sent, received);
}

TEST(LLVMTargetAdapterTest, Connect) {
  LLVMTargetAdapter adapter;

  auto ptr = MakeSyncPtr(adapter);
  FakeSignalCoordinator coordinator;
  SharedMemory test_input;
  test_input.Reserve(1 << 12);
  EXPECT_EQ(ptr->Connect(coordinator.Create(), test_input.Share()), ZX_OK);

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
}

TEST(LLVMTargetAdapterTest, Run) {
  LLVMTargetAdapter adapter;
  std::atomic<bool> done(false);
  std::thread run_thread([&]() {
    adapter.Run();
    done = true;
  });

  auto ptr = MakeSyncPtr(adapter);
  FakeSignalCoordinator coordinator;
  SharedMemory test_input;
  test_input.Reserve(1 << 12);
  EXPECT_EQ(ptr->Connect(coordinator.Create(), test_input.Share()), ZX_OK);

  EXPECT_FALSE(done.load());
  ptr.Unbind();
  run_thread.join();
  EXPECT_TRUE(done.load());
}

}  // namespace
}  // namespace fuzzing
