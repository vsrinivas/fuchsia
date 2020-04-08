// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/device_id_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>

#include <optional>
#include <string>

#include "src/developer/feedback/testing/stubs/device_id_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace fidl {
namespace {

constexpr zx::duration kDefaultTimeout = zx::sec(35);

constexpr char kDefaultDeviceId[] = "device_id";

class DeviceIdProviderPtrTest : public UnitTestFixture {
 public:
  DeviceIdProviderPtrTest()
      : UnitTestFixture(),
        executor_(dispatcher()),
        device_id_provider_ptr_(dispatcher(), services()) {}

 protected:
  void SetUpDeviceIdProviderServer(
      std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server) {
    device_id_provider_server_ = std::move(device_id_provider_server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  std::optional<std::string> GetId() {
    bool is_called = false;
    std::optional<std::string> device_id = std::nullopt;
    executor_.schedule_task(device_id_provider_ptr_.GetId(kDefaultTimeout)
                                .then([&](::fit::result<std::string>& result) {
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

  DeviceIdProviderPtr device_id_provider_ptr_;
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
};

TEST_F(DeviceIdProviderPtrTest, Check_DeviceIsCachedInConstructor) {
  SetUpDeviceIdProviderServer(
      std::make_unique<stubs::DeviceIdProviderExpectsOneCall>(kDefaultDeviceId));
  RunLoopUntilIdle();
}

TEST_F(DeviceIdProviderPtrTest, Check_CachedDeviceIdReturned) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  RunLoopUntilIdle();

  const std::optional<std::string> id = GetId();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), kDefaultDeviceId);
}

TEST_F(DeviceIdProviderPtrTest, Check_ErrorCachedInConstructor) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderReturnsError>());
  RunLoopUntilIdle();
}

TEST_F(DeviceIdProviderPtrTest, Check_CachedErrorReturned) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderReturnsError>());
  RunLoopUntilIdle();

  EXPECT_FALSE(GetId().has_value());
}

TEST_F(DeviceIdProviderPtrTest, Check_ErrorOnTimeout) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderNeverReturns>());

  bool is_called = false;
  std::optional<std::string> device_id = std::nullopt;
  executor_.schedule_task(
      device_id_provider_ptr_.GetId(kDefaultTimeout).then([&](::fit::result<std::string>& result) {
        is_called = true;

        if (result.is_ok()) {
          device_id = result.take_value();
        }
      }));
  RunLoopFor(kDefaultTimeout);
  FX_CHECK(is_called) << "The promise chain was never executed";

  EXPECT_FALSE(device_id.has_value());
}

TEST_F(DeviceIdProviderPtrTest, Check_SuccessOnSecondAttempt) {
  SetUpDeviceIdProviderServer(
      std::make_unique<stubs::DeviceIdProviderClosesFirstConnection>(kDefaultDeviceId));
  RunLoopUntilIdle();

  // We need to run the loop for longer than the exponential backoff becuase the backoff is
  // nondeterministic.
  RunLoopFor(zx::msec(100) /*minimum backoff*/ * 2);
  const std::optional<std::string> id = GetId();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), kDefaultDeviceId);
}

TEST_F(DeviceIdProviderPtrTest, Check_ReturnErrorOnNoServer) {
  bool is_called = false;
  std::optional<std::string> device_id = std::nullopt;
  executor_.schedule_task(
      device_id_provider_ptr_.GetId(kDefaultTimeout).then([&](::fit::result<std::string>& result) {
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
}  // namespace fidl
}  // namespace feedback
