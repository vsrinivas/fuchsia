// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/device_id_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fpromise/result.h>

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/device_id_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
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
                                .then([&](::fpromise::result<std::string, Error>& result) {
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

TEST_F(DeviceIdProviderPtrTest, Check_CachedDeviceIdReturned) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDefaultDeviceId));
  RunLoopUntilIdle();

  const std::optional<std::string> id = GetId();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), kDefaultDeviceId);
}

}  // namespace
}  // namespace fidl
}  // namespace forensics
