// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/adapters/llvm.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"

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

using ::fuchsia::fuzzer::TargetAdapterPtr;

class LLVMTargetAdapterTest : public AsyncTest {
 protected:
  TargetAdapterPtr MakePtr(LLVMTargetAdapter& adapter) {
    TargetAdapterPtr ptr;
    auto handler = adapter.GetHandler();
    handler(ptr.NewRequest(executor()->dispatcher()));
    return ptr;
  }
};

// Unit tests.

TEST_F(LLVMTargetAdapterTest, GetParameters) {
  LLVMTargetAdapter adapter(executor());
  std::vector<std::string> parameters({"foo", "bar", "baz"});
  adapter.SetParameters(parameters);

  TargetAdapterPtr ptr;
  auto handler = adapter.GetHandler();
  handler(ptr.NewRequest(executor()->dispatcher()));

  Bridge<std::vector<std::string>> bridge;
  ptr->GetParameters(bridge.completer.bind());
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()), parameters);
  RunUntilIdle();
}

TEST_F(LLVMTargetAdapterTest, Connect) {
  LLVMTargetAdapter adapter(executor());
  TargetAdapterPtr ptr;
  auto handler = adapter.GetHandler();
  handler(ptr.NewRequest(executor()->dispatcher()));

  AsyncEventPair eventpair(executor());
  SharedMemory test_input;
  EXPECT_EQ(test_input.Reserve(1 << 12), ZX_OK);
  Bridge<> bridge;
  zx::vmo vmo;
  EXPECT_EQ(test_input.Share(&vmo), ZX_OK);
  ptr->Connect(eventpair.Create(), std::move(vmo), bridge.completer.bind());
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()));
  RunUntilIdle();
}

TEST_F(LLVMTargetAdapterTest, Run) {
  LLVMTargetAdapter adapter(executor());
  TargetAdapterPtr ptr;
  auto handler = adapter.GetHandler();
  handler(ptr.NewRequest(executor()->dispatcher()));

  // Call |Run| before connecting.
  FUZZING_EXPECT_OK(adapter.Run());

  std::vector<std::string> strings{"foo", "bar", "baz"};
  AsyncEventPair eventpair(executor());
  SharedMemory test_input;
  EXPECT_EQ(test_input.Reserve(1 << 12), ZX_OK);
  Bridge<> bridge;

  // Connect...
  zx::vmo vmo;
  EXPECT_EQ(test_input.Share(&vmo), ZX_OK);
  ptr->Connect(eventpair.Create(), std::move(vmo), bridge.completer.bind());
  auto task = bridge.consumer.promise_or(fpromise::error())
                  .and_then([&, run = 0U, finish = ZxFuture<zx_signals_t>()](
                                Context& context) mutable -> Result<> {
                    // ...perform 3 runs...
                    while (run < strings.size()) {
                      auto& s = strings[run];
                      if (!finish) {
                        // Include null terminator.
                        EXPECT_EQ(test_input.Write(s.data(), s.size() + 1), ZX_OK);
                        EXPECT_EQ(eventpair.SignalPeer(0, kStart), ZX_OK);
                        finish = eventpair.WaitFor(kFinish);
                      }
                      if (!finish(context)) {
                        return fpromise::pending();
                      }
                      EXPECT_TRUE(finish.is_ok());
                      EXPECT_EQ(eventpair.SignalSelf(finish.take_value(), 0), ZX_OK);
                      EXPECT_STREQ(last_input.data, s.data());
                      EXPECT_EQ(last_input.size, test_input.size());
                      ++run;
                    }
                    return fpromise::ok();
                  })
                  .and_then([&] {
                    // ...and then disconnect.
                    eventpair.Reset();
                    return fpromise::ok();
                  });
  FUZZING_EXPECT_OK(std::move(task));
  RunUntilIdle();
}

}  // namespace
}  // namespace fuzzing
