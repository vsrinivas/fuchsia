// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "llvm-fuzzer.h"

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <stdint.h>

#include <thread>

#include "test/test-data-provider.h"

namespace fuzzing {

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class LlvmFuzzerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    context_ = provider_.TakeContext();
    context_->outgoing()->AddPublicService(data_provider_impl_.GetHandler());
    provider_.ConnectToPublicService(data_provider_ptr_.NewRequest());
  }

 protected:
  TestDataProvider data_provider_impl_;
  DataProviderPtr data_provider_ptr_;
  LlvmFuzzerImpl llvm_fuzzer_;

 private:
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<sys::ComponentContext> context_;
};

// Provide implementations of required symbols. These are typically auto-generated or provided by
// the fuzzer author.

std::vector<std::string> LlvmFuzzerImpl::GetDataConsumerLabels() {
  return std::vector<std::string>{"foo", "bar"};
}

}  // namespace fuzzing

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int total = 0;
  for (size_t i = 0; i < size; ++i) {
    total += data[i];
  }
  return total;
}

namespace fuzzing {

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit test

TEST_F(LlvmFuzzerTest, Configure) {
  // Unbound interface ptr
  DataProviderPtr data_provider;
  EXPECT_EQ(llvm_fuzzer_.Configure(std::move(data_provider)), ZX_ERR_INVALID_ARGS);

  // Valid
  EXPECT_EQ(llvm_fuzzer_.Configure(std::move(data_provider_ptr_)), ZX_OK);
  RunLoopUntilIdle();

  EXPECT_TRUE(data_provider_impl_.HasLabel(""));
  EXPECT_TRUE(data_provider_impl_.IsMapped(""));
}

TEST_F(LlvmFuzzerTest, TestOneInput) {
  EXPECT_EQ(llvm_fuzzer_.Configure(std::move(data_provider_ptr_)), ZX_OK);
  RunLoopUntilIdle();

  // TestOneInput works without data.
  std::string data;
  data_provider_impl_.PartitionTestInput(data.c_str(), data.size());
  int actual = -1;
  llvm_fuzzer_.TestOneInput([&actual](int result) { actual = result; });
  EXPECT_EQ(actual, 0);

  // TestOneInput works with data
  data = "ABCD";
  data_provider_impl_.PartitionTestInput(data.c_str(), data.size());
  llvm_fuzzer_.TestOneInput([&actual](int result) { actual = result; });
  EXPECT_EQ(actual, 266 /* 0x41 + 0x42 + 0x43 + 0x44*/);
}

}  // namespace fuzzing
