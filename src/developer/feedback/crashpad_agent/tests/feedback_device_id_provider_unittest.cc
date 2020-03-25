// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/feedback_device_id_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>

#include <optional>
#include <string>

#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_device_id_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr zx::duration kDefaultTimeout = zx::sec(35);

constexpr char kDefaultDeviceId[] = "device_id";

class FeedbackDeviceIdProviderTest : public UnitTestFixture {
 public:
  FeedbackDeviceIdProviderTest()
      : UnitTestFixture(), executor_(dispatcher()), device_id_provider_(dispatcher(), services()) {}

 protected:
  void SetUpStubFeedbackDeviceIdProvider(
      std::unique_ptr<StubFeedbackDeviceIdProvider> stub_feedback_device_id_provider) {
    stub_feedback_device_id_provider_ = std::move(stub_feedback_device_id_provider);
    if (stub_feedback_device_id_provider_) {
      InjectServiceProvider(stub_feedback_device_id_provider_.get());
    }
  }

  std::optional<std::string> GetId() {
    bool is_called = false;
    std::optional<std::string> device_id = std::nullopt;
    executor_.schedule_task(
        device_id_provider_.GetId(kDefaultTimeout).then([&](fit::result<std::string>& result) {
          is_called = true;

          if (result.is_ok()) {
            device_id = result.take_value();
          }
        }));
    RunLoopUntilIdle();
    FX_CHECK(is_called) << "The promise chain was never executed";

    return device_id;
  }

  async::Executor executor_;

  FeedbackDeviceIdProvider device_id_provider_;
  std::unique_ptr<StubFeedbackDeviceIdProvider> stub_feedback_device_id_provider_;
};

TEST_F(FeedbackDeviceIdProviderTest, Check_DeviceIsCachedInConstructor) {
  SetUpStubFeedbackDeviceIdProvider(
      std::make_unique<StubFeedbackDeviceIdProviderExpectsOneCall>(kDefaultDeviceId));
  RunLoopUntilIdle();
}

TEST_F(FeedbackDeviceIdProviderTest, Check_CachedDeviceIdReturned) {
  SetUpStubFeedbackDeviceIdProvider(
      std::make_unique<StubFeedbackDeviceIdProvider>(kDefaultDeviceId));
  RunLoopUntilIdle();

  const std::optional<std::string> id = GetId();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), kDefaultDeviceId);
}

TEST_F(FeedbackDeviceIdProviderTest, Check_ErrorCachedInConstructor) {
  SetUpStubFeedbackDeviceIdProvider(std::make_unique<StubFeedbackDeviceIdProviderReturnsError>());
  RunLoopUntilIdle();
}

TEST_F(FeedbackDeviceIdProviderTest, Check_CachedErrorReturned) {
  SetUpStubFeedbackDeviceIdProvider(std::make_unique<StubFeedbackDeviceIdProviderReturnsError>());
  RunLoopUntilIdle();

  EXPECT_FALSE(GetId().has_value());
}

TEST_F(FeedbackDeviceIdProviderTest, Check_ErrorOnTimeout) {
  SetUpStubFeedbackDeviceIdProvider(
      std::make_unique<StubFeedbackDeviceIdProviderNeverReturns>(kDefaultDeviceId));

  bool is_called = false;
  std::optional<std::string> device_id = std::nullopt;
  executor_.schedule_task(
      device_id_provider_.GetId(kDefaultTimeout).then([&](fit::result<std::string>& result) {
        is_called = true;

        if (result.is_ok()) {
          device_id = result.take_value();
        }
      }));
  RunLoopFor(kDefaultTimeout);
  FX_CHECK(is_called) << "The promise chain was never executed";

  EXPECT_FALSE(device_id.has_value());
}

TEST_F(FeedbackDeviceIdProviderTest, Check_SuccessOnSecondAttempt) {
  SetUpStubFeedbackDeviceIdProvider(
      std::make_unique<StubFeedbackDeviceIdProviderClosesFirstConnection>(kDefaultDeviceId));
  RunLoopUntilIdle();

  // We need to run the loop for longer than the exponential backoff becuase the backoff is
  // nondeterministic.
  RunLoopFor(zx::msec(100) /*minimum backoff*/ * 2);
  const std::optional<std::string> id = GetId();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), kDefaultDeviceId);
}

TEST_F(FeedbackDeviceIdProviderTest, Check_ReturnErrorOnNoStub) {
  bool is_called = false;
  std::optional<std::string> device_id = std::nullopt;
  executor_.schedule_task(
      device_id_provider_.GetId(kDefaultTimeout).then([&](fit::result<std::string>& result) {
        is_called = true;

        if (result.is_ok()) {
          device_id = result.take_value();
        }
      }));
  RunLoopFor(kDefaultTimeout);
  FX_CHECK(is_called) << "The promise chain was never executed";

  EXPECT_FALSE(device_id.has_value());
}

}  // namespace
}  // namespace feedback
