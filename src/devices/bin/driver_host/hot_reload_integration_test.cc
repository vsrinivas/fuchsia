// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/device/restarttest/llcpp/fidl.h>
#include <fuchsia/device/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "fbl/unique_fd.h"
#include "lib/sys/cpp/service_directory.h"
#include "src/devices/bin/driver_host/test-metadata.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace {

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device_restarttest::TestDevice;

void SetupEnvironment(board_test::DeviceEntry dev, driver_integration_test::IsolatedDevmgr* devmgr,
                      fuchsia::device::manager::DriverHostDevelopmentSyncPtr* development_) {
  driver_integration_test::IsolatedDevmgr::Args args;
  args.device_list.push_back(dev);

  ASSERT_OK(IsolatedDevmgr::Create(&args, devmgr));
  ASSERT_NE(devmgr->svc_root_dir().channel(), ZX_HANDLE_INVALID);

  zx::channel device_channel, remote;
  ASSERT_EQ(zx::channel::create(0, &device_channel, &remote), ZX_OK);

  // Connect to the DriverHostDevelopment service.
  zx::channel local;
  ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);

  std::string svc_name =
      fxl::StringPrintf("svc/%s", fuchsia::device::manager::DriverHostDevelopment::Name_);
  zx::status svc = service::Clone(devmgr->svc_root_dir());
  ASSERT_EQ(svc.status_value(), ZX_OK);
  sys::ServiceDirectory svc_dir(svc->TakeChannel());
  zx_status_t status = svc_dir.Connect(svc_name, std::move(remote));
  ASSERT_EQ(status, ZX_OK);

  development_->Bind(std::move(local));
}

// Test restarting a driver host containing only one driver.
TEST(HotReloadIntegrationTest, TestRestartOneDriver) {
  driver_integration_test::IsolatedDevmgr devmgr;
  fuchsia::device::manager::DriverHostDevelopmentSyncPtr development_;

  // Test device to add to devmgr.
  board_test::DeviceEntry dev = {};
  struct devhost_test_metadata test_metadata;
  dev.metadata = reinterpret_cast<const uint8_t*>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_RESTART_TEST;
  dev.did = 0;

  // Setup the environment for testing.
  SetupEnvironment(dev, &devmgr, &development_);

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

  // Need to create a DirWatcher to wait for the device to close.
  fbl::unique_fd fd(
      openat(devmgr.devfs_root().get(), "sys/platform/11:17:0", O_DIRECTORY | O_RDONLY));
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  ASSERT_OK(devmgr_integration_test::DirWatcher::Create(std::move(fd), &watcher));

  // Restart the driver host of the test driver.
  fuchsia::device::manager::DriverHostDevelopment_RestartDriverHosts_Result result;
  auto resp =
      development_->RestartDriverHosts("/boot/driver/driver-host-restart-driver.so", &result);
  ASSERT_OK(resp);

  // Make sure device has shut so that it isnt opened before it is restarted.
  ASSERT_OK(watcher->WaitForRemoval("driver-host-restart-driver", zx::duration::infinite()));

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

// Test restarting a driver host containing a parent and child driver by calling restart on
// the parent.
TEST(HotReloadIntegrationTest, TestRestartTwoDriversParent) {
  driver_integration_test::IsolatedDevmgr devmgr;
  fuchsia::device::manager::DriverHostDevelopmentSyncPtr development_;

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

  // Setup the environment for testing.
  SetupEnvironment(dev, &devmgr, &development_);

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

  // Need to create DirWatchers to wait for the device to close.
  fbl::unique_fd fd_watcher(
      openat(devmgr.devfs_root().get(), "sys/platform/11:0e:0", O_DIRECTORY | O_RDONLY));
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  ASSERT_OK(devmgr_integration_test::DirWatcher::Create(std::move(fd_watcher), &watcher));

  // Restart the driver host of the parent driver.
  fuchsia::device::manager::DriverHostDevelopment_RestartDriverHosts_Result result;
  auto resp = development_->RestartDriverHosts("/boot/driver/driver-host-test-driver.so", &result);
  ASSERT_OK(resp);

  // Make sure device has shut so that it isn't opened before it is restarted.
  // Child is a subdirectory of this so if the parent is gone so must the child.
  ASSERT_OK(watcher->WaitForRemoval("devhost-test-parent", zx::duration::infinite()));

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
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &fd_child));
  ASSERT_GT(fd_child.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_child.release(), chan_child.reset_and_get_address()));
  ASSERT_NE(chan_child.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_child.is_valid());
}

// Test restarting a driver host containing a parent and child driver by calling restart on
// the child.
TEST(HotReloadIntegrationTest, TestRestartTwoDriversChild) {
  driver_integration_test::IsolatedDevmgr devmgr;
  fuchsia::device::manager::DriverHostDevelopmentSyncPtr development_;

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

  // Setup the environment for testing.
  SetupEnvironment(dev, &devmgr, &development_);

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

  // Need to create DirWatchers to wait for the device to close.
  fbl::unique_fd fd_watcher(
      openat(devmgr.devfs_root().get(), "sys/platform/11:0e:0", O_DIRECTORY | O_RDONLY));
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  ASSERT_OK(devmgr_integration_test::DirWatcher::Create(std::move(fd_watcher), &watcher));

  // Get pid of parent driver before restarting.
  auto parent_before = TestDevice::Call::GetPid(zx::unowned(chan_parent));
  ASSERT_OK(parent_before.status());
  ASSERT_FALSE(parent_before->result.is_err(), "GetPid for parent failed: %s",
               zx_status_get_string(parent_before->result.err()));

  // Restart the driver host of the child driver.
  fuchsia::device::manager::DriverHostDevelopment_RestartDriverHosts_Result result;
  auto resp =
      development_->RestartDriverHosts("/boot/driver/driver-host-test-child-driver.so", &result);
  ASSERT_OK(resp);

  // Make sure device has shut so that it isn't opened before it is restarted.
  // Child is a subdirectory of this so if the parent is gone so must the child.
  ASSERT_OK(watcher->WaitForRemoval("devhost-test-parent", zx::duration::infinite()));

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
      devmgr.devfs_root(), "sys/platform/11:0e:0/devhost-test-parent/devhost-test-child",
      &fd_child));
  ASSERT_GT(fd_child.get(), 0);
  ASSERT_OK(fdio_get_service_handle(fd_child.release(), chan_child.reset_and_get_address()));
  ASSERT_NE(chan_child.get(), ZX_HANDLE_INVALID);
  ASSERT_TRUE(chan_child.is_valid());
}

}  // namespace
