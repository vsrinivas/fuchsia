// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <vector>

#include "llvm-fuzzer.h"
#include "sanitizer-cov-proxy.h"
#include "test/fake-inline-8bit-counters.h"
#include "test/fake-sanitizer-cov-proxy.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoveragePtr;
using ::fuchsia::fuzzer::DataProviderPtr;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class EngineTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Start the engine, and publish the Coverage and DataProvider services.
    engine_ = std::make_unique<TestEngine>(provider_.TakeContext(), dispatcher());

    // Start the client, and connect to the DataProvider service.
    DataProviderPtr data_provider;
    provider_.ConnectToPublicService(data_provider.NewRequest());
    llvm_fuzzer_.Configure(std::move(data_provider));
    RunLoopUntilIdle();

    // Start the proxy, and connect to the Coverage service.
    FakeSanitizerCovProxy::Reset();
    auto proxy = SanitizerCovProxy::GetInstance(false /* autoconnect */);

    CoveragePtr coverage;
    provider_.ConnectToPublicService(coverage.NewRequest());
    ASSERT_EQ(proxy->SetCoverage(std::move(coverage)), ZX_OK);

    // Fake call to __sanitizer_cov_8bit_counters_init.
    while (!FakeInline8BitCounters::Reset()) {
      RunLoopUntilIdle();
    }
  }

  // Test helper that does a complete roundtrip from the DataProvider in the engine, to the fuzz
  // target function, to the SanitizerCovProxy, back to the Coverage in the engine.
  void PerformFuzzingIteration(const std::vector<uint8_t> &data);

 protected:
  class TestEngine : public Engine {
   public:
    explicit TestEngine(std::unique_ptr<sys::ComponentContext> context,
                        async_dispatcher_t *dispatcher)
        : Engine(std::move(context), dispatcher) {}
  };

  LlvmFuzzerImpl llvm_fuzzer_;

 private:
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<TestEngine> engine_;
};

// Provide implementations of required symbols. These are typically auto-generated or provided by
// the fuzzer author.

std::vector<std::string> LlvmFuzzerImpl::GetDataConsumerLabels() {
  return std::vector<std::string>{};
}

}  // namespace fuzzing

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return fuzzing::FakeInline8BitCounters::Write(data, size);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests

namespace fuzzing {

void EngineTest::PerformFuzzingIteration(const std::vector<uint8_t> &data) {
  // Have the engine run once on a test inpue. This is a blocking call, so it must happen in another
  // thread for the test to still be able to drive the loop.
  int result = -1;
  sync_completion_t sync;
  std::thread t2([this, &data, &result, &sync]() {
    result = engine_->RunOne(&data[0], data.size());
    sync_completion_signal(&sync);
  });

  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) != ZX_OK) {
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
    RunLoopUntilIdle();
  }
  t2.join();

  EXPECT_EQ(result, 0);
  for (size_t i = 0; i < data.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(FakeInline8BitCounters::At(i), data[i]);
  }
}

TEST_F(EngineTest, RunOne) {
  PerformFuzzingIteration({0x01, 0x02, 0x03, 0x04});
  PerformFuzzingIteration({});
  PerformFuzzingIteration({0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef});
}

}  // namespace fuzzing
