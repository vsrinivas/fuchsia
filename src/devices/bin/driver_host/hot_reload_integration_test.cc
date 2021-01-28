// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/device/restarttest/llcpp/fidl.h>
#include <fuchsia/device/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_host/test-metadata.h"

namespace {

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::restarttest::TestDevice;

// Test restarting a driver host containing only one driver.
TEST(HotReloadIntegrationTest, DISABLED_TestRestartOneDriver) {
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  // Test device to add to devmgr.
  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata;
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_RESTART_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  // Create the device manager.
  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd fd_driver;
  zx::channel chan_driver;

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:17:0/driver-host-restart-driver", &fd_driver));
  ASSERT_GT(fd_driver.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_driver.release(), chan_driver.reset_and_get_address()));
  ASSERT_NE(chan_driver.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_driver.is_valid());

  // Get pid of driver before restarting.
  auto result_before = TestDevice::Call::GetPid(zx::unowned(chan_driver));
  ASSERT_OK(result_before.status());
  ASSERT_FALSE(result_before->result.is_err(), "GetPid failed: %s",
               zx_status_get_string(result_before->result.err()));

  auto resp =
      ::llcpp::fuchsia::device::manager::DevhostController::Call::Restart(zx::unowned(chan_driver));
  ASSERT_OK(resp.status());

  // Get pid of driver after restarting.
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:17:0/driver-host-restart-driver", &fd_driver));
  ASSERT_GT(fd_driver.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_driver.release(), chan_driver.reset_and_get_address()));
  ASSERT_NE(chan_driver.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_driver.is_valid());

  auto result_after = TestDevice::Call::GetPid(zx::unowned(chan_driver));
  ASSERT_OK(result_after.status());
  ASSERT_FALSE(result_after->result.is_err(), "GetPid failed: %s",
               zx_status_get_string(result_after->result.err()));

  ASSERT_NE(result_before.value().result.response().pid,
            result_after.value().result.response().pid);
}

// Test restarting a driver host containing a parent and child driver.
TEST(HotReloadIntegrationTest, DISABLED_TestRestartTwoDrivers) {
  driver_integration_test::IsolatedDevmgr::Args args;
  driver_integration_test::IsolatedDevmgr devmgr;

  // Test device to add to devmgr.
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

  // Create the device manager.
  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));

  fbl::unique_fd fd_parent, fd_child;
  zx::channel chan_parent, chan_child;

  // Open parent.
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &fd_parent));
  ASSERT_GT(fd_parent.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_parent.release(), chan_parent.reset_and_get_address()));
  ASSERT_NE(chan_parent.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_parent.is_valid());

  // Open child.
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &fd_child));
  ASSERT_GT(fd_child.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_child.release(), chan_child.reset_and_get_address()));
  ASSERT_NE(chan_child.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_child.is_valid());

  // Get pid of parent driver before restarting.
  auto parent_before = TestDevice::Call::GetPid(zx::unowned(chan_parent));
  ASSERT_OK(parent_before.status());
  ASSERT_FALSE(parent_before->result.is_err(), "GetPid for parent failed: %s",
               zx_status_get_string(parent_before->result.err()));
  auto resp =
      ::llcpp::fuchsia::device::manager::DevhostController::Call::Restart(zx::unowned(chan_parent));
  ASSERT_OK(resp.status());

  // Reopen parent.
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent", &fd_parent));
  ASSERT_GT(fd_parent.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_parent.release(), chan_parent.reset_and_get_address()));
  ASSERT_NE(chan_parent.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_parent.is_valid());

  // Get pid of parent driver after restarting.
  auto parent_after = TestDevice::Call::GetPid(zx::unowned(chan_parent));
  ASSERT_OK(parent_after.status());
  ASSERT_FALSE(parent_after->result.is_err(), "GetPid for parent failed: %s",
               zx_status_get_string(parent_after->result.err()));

  // Check pid of parent has changed.
  ASSERT_NE(parent_before.value().result.response().pid,
            parent_after.value().result.response().pid);

  // Check child has reopened.
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-child", &fd_child));
  ASSERT_GT(fd_child.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_child.release(), chan_child.reset_and_get_address()));
  ASSERT_NE(chan_child.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_child.is_valid());
}

}  // namespace
