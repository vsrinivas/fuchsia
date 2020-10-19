// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/hanging_get_ptr.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace fidl {
namespace {

class HangingGetDeviceIdProviderPtr {
 public:
  HangingGetDeviceIdProviderPtr(async_dispatcher_t* dispatcher,
                                std::shared_ptr<sys::ServiceDirectory> services)
      : connection_(dispatcher, services, [this] { GetDeviceIdProvider(); }) {}

  ::fit::promise<std::string, Error> GetDeviceIdProvider(zx::duration timeout) {
    return connection_.GetValue(fit::Timeout(timeout));
  }

 private:
  void GetDeviceIdProvider() {
    connection_->GetId([this](std::string device_id) {
      if (!device_id.empty()) {
        connection_.SetValue(device_id);
      } else {
        connection_.SetError(Error::kMissingValue);
      }
    });
  }

  HangingGetPtr<fuchsia::feedback::DeviceIdProvider, std::string> connection_;
};

constexpr char kDeviceId[] = "device-id";
constexpr zx::duration kTimeout = zx::sec(1);

// We need to use an actual FIDL interface to test HangingGetPtr, so we use
// fuchsia::feedback::DeviceIdProvider and stubs::DeviceIdProvider in our test cases.
class HangingGetPtrTest : public UnitTestFixture {
 public:
  HangingGetPtrTest() : executor_(dispatcher()), device_id_provider_server_() {}

 protected:
  template <typename V, typename E>
  ::fit::result<V, E> ExecutePromise(::fit::promise<V, E> promise) {
    ::fit::result<V, E> out_result;
    executor_.schedule_task(std::move(promise).then(
        [&](::fit::result<V, E>& result) { out_result = std::move(result); }));
    RunLoopFor(kTimeout);
    return out_result;
  }

  void SetUpDeviceIdProviderServer(
      std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server) {
    device_id_provider_server_ = std::move(device_id_provider_server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  void UpdateDeviceId(std::string device_id) {
    device_id_provider_server_->SetDeviceId(std::move(device_id));
  }

 private:
  async::Executor executor_;
  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
};

TEST_F(HangingGetPtrTest, Check_CachesValueInConstructor) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDeviceId));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_ok());
    EXPECT_EQ(device_id.value(), kDeviceId);
  }
}

TEST_F(HangingGetPtrTest, Check_SubsequentCallsHangs) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDeviceId));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_ok());
    EXPECT_EQ(device_id.value(), kDeviceId);
  }

  UpdateDeviceId("device-id-2");
  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_ok());
    EXPECT_EQ(device_id.value(), "device-id-2");
  }

  UpdateDeviceId("device-id-3");
  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_ok());
    EXPECT_EQ(device_id.value(), "device-id-3");
  }
}

TEST_F(HangingGetPtrTest, Check_CachesErrorInConstructor) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(""));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_error());
    EXPECT_EQ(device_id.error(), Error::kMissingValue);
  }
}

TEST_F(HangingGetPtrTest, Check_SubsequentCallsFixError) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(""));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_error());
    EXPECT_EQ(device_id.error(), Error::kMissingValue);
  }

  UpdateDeviceId("device-id-2");
  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

    ASSERT_TRUE(device_id.is_ok());
    EXPECT_EQ(device_id.value(), "device-id-2");
  }
}

TEST_F(HangingGetPtrTest, Check_ErrorOnTimeout) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());

  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderNeverReturns>());

  const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

  ASSERT_TRUE(device_id.is_error());
  EXPECT_EQ(device_id.error(), Error::kTimeout);
}

TEST_F(HangingGetPtrTest, Check_SuccessOnSecondAttempt) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());
  SetUpDeviceIdProviderServer(
      std::make_unique<stubs::DeviceIdProviderClosesFirstConnection>(kDeviceId));

  RunLoopUntilIdle();

  // We set the timeout to be larger than the backoff so we're guarenteed to have a value
  auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(zx::sec(1)));

  ASSERT_TRUE(device_id.is_ok());
  EXPECT_EQ(device_id.value(), kDeviceId);
}

TEST_F(HangingGetPtrTest, Check_ReturnErrorOnNoServer) {
  HangingGetDeviceIdProviderPtr device_id_provider_ptr(dispatcher(), services());

  SetUpDeviceIdProviderServer(nullptr);

  const auto device_id = ExecutePromise(device_id_provider_ptr.GetDeviceIdProvider(kTimeout));

  ASSERT_TRUE(device_id.is_error());
  EXPECT_EQ(device_id.error(), Error::kTimeout);
}

}  // namespace
}  // namespace fidl
}  // namespace forensics
