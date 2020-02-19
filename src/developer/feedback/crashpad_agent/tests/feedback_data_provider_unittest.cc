// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/feedback_data_provider.h"

#include <lib/async/cpp/executor.h>

#include "lib/fit/result.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_data_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Data;

constexpr zx::duration kDefaultTimeout = zx::sec(35);

class FeedbackDataProviderTest : public UnitTestFixture {
 public:
  FeedbackDataProviderTest()
      : UnitTestFixture(), executor_(dispatcher()), data_provider_(dispatcher(), services()) {}

 protected:
  // Sets up the underlying feedback data provider and registers it in the
  // |service_directory_provider_|.
  void SetUpStubFeedbackDataProvider(
      std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider) {
    stub_feedback_data_provider_ = std::move(stub_feedback_data_provider);
    if (stub_feedback_data_provider_) {
      InjectServiceProvider(stub_feedback_data_provider_.get());
    }
  }

  size_t total_num_feedback_data_provider_bindings() {
    return stub_feedback_data_provider_->total_num_bindings();
  }

  bool is_feedback_data_provider_bound() { return stub_feedback_data_provider_->is_bound(); }

  std::vector<fit::result<Data>> GetFeedbackData(size_t num_parallel_calls) {
    std::vector<fit::result<Data>> results(num_parallel_calls);
    for (auto& result : results) {
      executor_.schedule_task(
          data_provider_.GetData(kDefaultTimeout).then([&](fit::result<Data>& data) {
            result = std::move(data);
          }));
    }
    RunLoopUntilIdle();
    return results;
  }

  void CloseConnection() { stub_feedback_data_provider_->CloseConnection(); }

  async::Executor& executor() { return executor_; }
  FeedbackDataProvider& data_provider() { return data_provider_; }

 private:
  async::Executor executor_;
  FeedbackDataProvider data_provider_;
  std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider_;
};

TEST_F(FeedbackDataProviderTest, Check_DataProviderConnectionIsReused) {
  const size_t num_calls = 5u;
  // We use a stub that returns no data as we are not interested in the payload, just the number of
  // different connections to the stub.
  SetUpStubFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  const std::vector<fit::result<Data>> results = GetFeedbackData(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_error());
  }

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), 1u);
  EXPECT_FALSE(is_feedback_data_provider_bound());
}

TEST_F(FeedbackDataProviderTest, Check_DataProviderReconnectsCorrectly) {
  const size_t num_calls = 5u;
  // We use a stub that returns no data as we are not interested in the payload, just the number of
  // different connections to the stub.
  SetUpStubFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  std::vector<fit::result<Data>> results = GetFeedbackData(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_error());
  }

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), 1u);
  EXPECT_FALSE(is_feedback_data_provider_bound());

  results.clear();
  results = GetFeedbackData(num_calls);

  ASSERT_EQ(results.size(), num_calls);
  for (const auto& result : results) {
    EXPECT_TRUE(result.is_error());
  }

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), 2u);
  EXPECT_FALSE(is_feedback_data_provider_bound());
}

TEST_F(FeedbackDataProviderTest, Fail_OnNoFeedbackDataProvider) {
  const size_t num_calls = 1u;

  // We pass a nullptr stub so there will be no fuchsia.feedback.DataProvider service to connect to.
  SetUpStubFeedbackDataProvider(nullptr);

  std::vector<fit::result<Data>> results = GetFeedbackData(num_calls);
  ASSERT_EQ(results.size(), num_calls);
  EXPECT_TRUE(results[0].is_error());
}

TEST_F(FeedbackDataProviderTest, Fail_OnFeedbackDataProviderTakingTooLong) {
  const size_t num_calls = 1u;

  SetUpStubFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderNeverReturning>());

  std::vector<fit::result<Data>> results = GetFeedbackData(num_calls);
  RunLoopFor(kDefaultTimeout);

  ASSERT_EQ(results.size(), num_calls);
  EXPECT_TRUE(results[0].is_error());
}

}  // namespace
}  // namespace feedback
