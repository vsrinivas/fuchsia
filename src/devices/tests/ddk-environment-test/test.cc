// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/environment/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/syscalls.h>

#include <unordered_set>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device_environment_test::TestDevice;

class EnvironmentTest : public zxtest::Test {
 public:
  ~EnvironmentTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/ddk-environment-test.so");

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_ENVIRONMENT_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr_.devfs_root(), "sys/platform/11:14:0/ddk-environment-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);
  }

 protected:
  zx::channel chan_;
  IsolatedDevmgr devmgr_;
};

TEST_F(EnvironmentTest, GetServiceList) {
  auto result = TestDevice::Call::GetServiceList(zx::unowned(chan_));
  ASSERT_OK(result.status());
  ASSERT_EQ(result->services.count(), 3);

  std::unordered_set<std::string> actual;
  for (const auto& service : result->services) {
    actual.emplace(service.data(), service.size());
  }
  std::unordered_set<std::string> kExpectedServices = {
      "/svc/fuchsia.logger.LogSink",
      "/svc/fuchsia.scheduler.ProfileProvider",
      "/svc/fuchsia.tracing.provider.Registry",
  };
  ASSERT_EQ(actual, kExpectedServices);
}
