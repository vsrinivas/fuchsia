// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>
#include <zircon/types.h>

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "src/ui/bin/hardware_display_controller_provider/fake/service.h"

namespace {

constexpr std::chrono::milliseconds kSleepTime(10);

class FakeHardwareDisplayControllerProviderTest : public gtest::TestLoopFixture {
 public:
  FakeHardwareDisplayControllerProviderTest() = default;
  ~FakeHardwareDisplayControllerProviderTest() override = default;

  void SetUp() override {
    TestLoopFixture::SetUp();

    service_ = std::make_unique<fake_display::ProviderService>(nullptr, dispatcher());
  }

  void TearDown() override {
    // TODO(fxbug.dev/66466): this shouldn't be necessary, but without it there will be ASAN
    // failures, as the controller connections established by the tests haven't finished being torn
    // down.
    while (service_->controller_claimed() || service_->virtcon_controller_claimed()) {
      std::this_thread::sleep_for(kSleepTime);
      RunLoopUntilIdle();
    }

    service_.reset();
    RunLoopUntilIdle();
  }

  fake_display::ProviderService* service() { return service_.get(); }

 private:
  std::unique_ptr<fake_display::ProviderService> service_;
};

struct Request {
  zx::channel client_device;
  zx::channel server_device;
  fidl::InterfacePtr<fuchsia::hardware::display::Controller> controller;
};

Request NewRequest() {
  Request req;
  auto status = zx::channel::create(0u, &req.client_device, &req.server_device);
  ZX_ASSERT(status == ZX_OK);

  return req;
}

}  // anonymous namespace

TEST_F(FakeHardwareDisplayControllerProviderTest, NoConflictWithVirtcon) {
  // Count the number of connections that were ever made.
  uint32_t num_connections = 0;
  uint32_t num_virtcon_connections = 0;

  auto req = NewRequest();
  service()->OpenController(std::move(req.server_device), req.controller.NewRequest(),
                            [&num_connections](zx_status_t status) { ++num_connections; });

  auto req2 = NewRequest();
  service()->OpenVirtconController(
      std::move(req2.server_device), req2.controller.NewRequest(),
      [&num_virtcon_connections](zx_status_t status) { ++num_virtcon_connections; });

  EXPECT_EQ(num_connections, 1U);
  EXPECT_EQ(num_virtcon_connections, 1U);
}

TEST_F(FakeHardwareDisplayControllerProviderTest, MultipleConnections) {
  // Count the number of connections that were ever made.
  uint32_t num_connections = 0;

  auto req = NewRequest();
  service()->OpenController(
      std::move(req.server_device), req.controller.NewRequest(),
      [&num_connections](zx_status_t status) { EXPECT_EQ(++num_connections, 1U); });

  auto req2 = NewRequest();
  service()->OpenController(
      std::move(req2.server_device), req2.controller.NewRequest(),
      [&num_connections](zx_status_t status) { EXPECT_EQ(++num_connections, 2U); });

  auto req3 = NewRequest();
  service()->OpenController(
      std::move(req3.server_device), req3.controller.NewRequest(),
      [&num_connections](zx_status_t status) { EXPECT_EQ(++num_connections, 3U); });

  EXPECT_EQ(num_connections, 1U);
  EXPECT_EQ(service()->num_queued_requests(), 2U);

  // Drop the first connection, which will enable the second connection to be made.
  req.controller.Unbind();
  req.client_device.reset();
  while (service()->num_queued_requests() == 2) {
    // Real wall clock time must elapse for the service to handle a kernel notification
    // that the channel has closed.
    std::this_thread::sleep_for(kSleepTime);
    RunLoopUntilIdle();
  }

  EXPECT_EQ(num_connections, 2U);
  EXPECT_EQ(service()->num_queued_requests(), 1U);

  // Drop the second connection, which will enable the third connection to be made.
  req2.controller.Unbind();
  req2.client_device.reset();
  while (service()->num_queued_requests() == 1) {
    // Real wall clock time must elapse for the service to handle a kernel notification
    // that the channel has closed.
    std::this_thread::sleep_for(kSleepTime);
    RunLoopUntilIdle();
  }

  EXPECT_EQ(num_connections, 3U);
  EXPECT_EQ(service()->num_queued_requests(), 0U);
}
