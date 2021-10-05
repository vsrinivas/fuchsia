// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <limits.h>

#include <map>

#include <ddk/metadata/test.h>
#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_host/test-metadata.h"

namespace {

using devmgr_integration_test::IsolatedDevmgr;

static constexpr const char kDriverTestDir[] = "/boot/driver";
static constexpr const char kPassDriverName[] = "unit-test-pass.so";
static constexpr const char kFailDriverName[] = "unit-test-fail.so";

void CreateTestDevice(const IsolatedDevmgr& devmgr, const char* driver_name,
                      zx::channel* dev_channel) {
  fbl::unique_fd root_fd;
  zx_status_t status =
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &root_fd);
  ASSERT_OK(status);

  fidl::WireSyncClient<fuchsia_device_test::RootDevice> test_root{zx::channel{}};
  status = fdio_get_service_handle(root_fd.release(),
                                   test_root.mutable_channel()->reset_and_get_address());
  ASSERT_OK(status);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto result =
      test_root.CreateDevice(fidl::StringView::FromExternal(driver_name), std::move(remote));
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  *dev_channel = std::move(local);
}

// Test binding second time
TEST(DeviceControllerIntegrationTest, TestDuplicateBindSameDriver) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);

  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);
  call_status = ZX_OK;
  auto resp2 = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                   ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp2.status());
  if (resp2->result.is_err()) {
    call_status = resp2->result.err();
  }
  ASSERT_OK(resp2.status());
  ASSERT_EQ(call_status, ZX_ERR_ALREADY_BOUND);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

TEST(DeviceControllerIntegrationTest, TestRebindNoChildrenManualBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Rebind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

TEST(DeviceControllerIntegrationTest, TestRebindChildrenAutoBind) {
  using driver_integration_test::IsolatedDevmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata = {
      .init_reply_success = true,
  };
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_DEVHOST_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);

  fbl::unique_fd test_fd, parent_fd, child_fd;
  zx::channel parent_channel;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                         "sys/platform/11:0e:0", &test_fd);
  status = devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd);

  ASSERT_OK(status);
  status = fdio_get_service_handle(parent_fd.release(), parent_channel.reset_and_get_address());
  ASSERT_OK(status);

  // Do not open the child. Otherwise rebind will be stuck.
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(parent_channel))
                  ->Rebind(::fidl::StringView(""));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &child_fd));
}

TEST(DeviceControllerIntegrationTest, TestRebindChildrenManualBind) {
  using driver_integration_test::IsolatedDevmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata = {
      .init_reply_success = true,
  };
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_DEVHOST_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd test_fd, parent_fd, child_fd;
  zx::channel parent_channel;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                          "sys/platform/11:0e:0", &test_fd));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(fdio_get_service_handle(parent_fd.release(), parent_channel.reset_and_get_address()));

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", "/boot/driver",
                     "driver-host-test-child-driver.so");
  // Do not open the child. Otherwise rebind will be stuck.
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(parent_channel))
                  ->Rebind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &child_fd));
}

TEST(DeviceControllerIntegrationTest, TestUnbindChildrenSuccess) {
  using driver_integration_test::IsolatedDevmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata = {
      .init_reply_success = true,
  };
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_DEVHOST_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd test_fd, parent_fd, child_fd;
  zx::channel parent_channel;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                          "sys/platform/11:0e:0", &test_fd));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(fdio_get_service_handle(parent_fd.release(), parent_channel.reset_and_get_address()));

  zx_status_t call_status = ZX_OK;
  auto resp =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned(parent_channel))->UnbindChildren();
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
}

// Test binding again, but with different driver
TEST(DeviceControllerIntegrationTest, TestDuplicateBindDifferentDriver) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);

  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  call_status = ZX_OK;
  len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  auto resp2 = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                   ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp2.status());
  if (resp2->result.is_err()) {
    call_status = resp2->result.err();
  }
  ASSERT_OK(resp2.status());
  ASSERT_EQ(call_status, ZX_ERR_ALREADY_BOUND);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

TEST(DeviceControllerIntegrationTest, AllTestsEnabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.boot_args.emplace("driver.tests.enable", "true");

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kPassDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kPassDriverName);
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

TEST(DeviceControllerIntegrationTest, AllTestsEnabledBindFail) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.boot_args.emplace("driver.tests.enable", "true");

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_BAD_STATE);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

// Test the flag using bind failure as a proxy for "the unit test did run".
TEST(DeviceControllerIntegrationTest, SpecificTestEnabledBindFail) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.boot_args.emplace("driver.unit_test_fail.tests.enable", "true");

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_BAD_STATE);
  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

// Test the flag using bind success as a proxy for "the unit test didn't run".
TEST(DeviceControllerIntegrationTest, DefaultTestsDisabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

// Test the flag using bind success as a proxy for "the unit test didn't run".
TEST(DeviceControllerIntegrationTest, SpecificTestDisabledBind) {
  IsolatedDevmgr devmgr;
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.boot_args.emplace("driver.tests.enable", "true");
  args.boot_args.emplace("driver.unit_test_fail.tests.enable", "false");

  zx_status_t status = IsolatedDevmgr::Create(std::move(args), &devmgr);
  ASSERT_OK(status);

  zx::channel dev_channel;
  CreateTestDevice(devmgr, kFailDriverName, &dev_channel);

  char libpath[PATH_MAX];
  int len = snprintf(libpath, sizeof(libpath), "%s/%s", kDriverTestDir, kFailDriverName);
  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(dev_channel))
                  ->Bind(::fidl::StringView(libpath, len));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);
  fidl::WireCall<fuchsia_device_test::Device>(zx::unowned_channel{dev_channel})->Destroy();
}

TEST(DeviceControllerIntegrationTest, TestRebindWithInit_Success) {
  using driver_integration_test::IsolatedDevmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata = {
      .init_reply_success = true,
  };
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_DEVHOST_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd test_fd, parent_fd, child_fd;
  zx::channel test_channel, parent_channel;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                          "sys/platform/11:0e:0", &test_fd));
  ASSERT_OK(fdio_get_service_handle(test_fd.release(), test_channel.reset_and_get_address()));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(fdio_get_service_handle(parent_fd.release(), parent_channel.reset_and_get_address()));

  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(parent_channel))
                  ->Rebind(::fidl::StringView(""));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_OK(call_status);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &child_fd));
}

TEST(DeviceControllerIntegrationTest, TestRebindWithInit_Failure) {
  using driver_integration_test::IsolatedDevmgr;
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata = {
      .init_reply_success = false,
  };
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_DEVHOST_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd test_fd, parent_fd, child_fd;
  zx::channel test_channel, parent_channel;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                          "sys/platform/11:0e:0", &test_fd));
  ASSERT_OK(fdio_get_service_handle(test_fd.release(), test_channel.reset_and_get_address()));
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
  ASSERT_OK(fdio_get_service_handle(parent_fd.release(), parent_channel.reset_and_get_address()));

  zx_status_t call_status = ZX_OK;
  auto resp = fidl::WireCall<fuchsia_device::Controller>(zx::unowned(parent_channel))
                  ->Rebind(::fidl::StringView(""));
  ASSERT_OK(resp.status());
  if (resp->result.is_err()) {
    call_status = resp->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_IO);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &parent_fd));
}

}  // namespace
