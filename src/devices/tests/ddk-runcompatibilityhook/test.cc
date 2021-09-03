// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "test-metadata.h"

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device::Controller;

TEST(DeviceControllerIntegrationTest, RunCompatibilityHookSuccess) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;

  board_test::DeviceEntry dev = {};
  struct compatibility_test_metadata test_metadata = {
      .add_in_bind = true,
      .remove_in_unbind = true,
      .remove_twice_in_unbind = false,
      .remove_in_suspend = false,
  };
  dev.metadata = reinterpret_cast<const uint8_t *>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_COMPATIBILITY_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0a:0/compatibility-test", &parent_fd);
  ASSERT_GT(parent_fd.get(), 0);
  devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0a:0/compatibility-test/compatibility-test-child",
      &child_fd);
  ASSERT_GT(child_fd.get(), 0);

  zx::channel parent_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(parent_fd.release(), parent_device_handle.reset_and_get_address()));
  ASSERT_TRUE((parent_device_handle.get() != ZX_HANDLE_INVALID), "");

  uint32_t call_status;
  auto resp = fidl::WireCall<Controller>(zx::unowned_channel(parent_device_handle.get()))
                  .RunCompatibilityTests(zx::duration::infinite().get());
  status = resp.status();
  call_status = resp->status;

  ASSERT_OK(status);
  ASSERT_EQ(call_status, fuchsia_device_manager_CompatibilityTestStatus_OK);
}

TEST(DeviceControllerIntegrationTest, RunCompatibilityHookMissingAddInBind) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;

  board_test::DeviceEntry dev = {};
  struct compatibility_test_metadata test_metadata = {
      .add_in_bind = false,
      .remove_in_unbind = true,
      .remove_twice_in_unbind = false,
      .remove_in_suspend = false,
  };
  dev.metadata = reinterpret_cast<const uint8_t *>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_COMPATIBILITY_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0a:0/compatibility-test", &parent_fd);
  ASSERT_GT(parent_fd.get(), 0);

  zx::channel parent_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(parent_fd.release(), parent_device_handle.reset_and_get_address()));
  ASSERT_TRUE((parent_device_handle.get() != ZX_HANDLE_INVALID), "");

  uint32_t call_status;
  auto resp = fidl::WireCall<Controller>(zx::unowned_channel(parent_device_handle.get()))
                  .RunCompatibilityTests(zx::duration(zx::msec(2000)).get());
  status = resp.status();
  call_status = resp->status;

  ASSERT_OK(status);
  ASSERT_EQ(call_status, fuchsia_device_manager_CompatibilityTestStatus_ERR_BIND_NO_DDKADD);
}

TEST(DeviceControllerIntegrationTest, RunCompatibilityHookMissingRemoveInUnbind) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;

  board_test::DeviceEntry dev = {};
  struct compatibility_test_metadata test_metadata = {
      .add_in_bind = true,
      .remove_in_unbind = false,
      .remove_twice_in_unbind = false,
      .remove_in_suspend = false,
  };
  dev.metadata = reinterpret_cast<const uint8_t *>(&test_metadata);
  dev.metadata_size = sizeof(test_metadata);
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_COMPATIBILITY_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0a:0/compatibility-test", &parent_fd);
  ASSERT_GT(parent_fd.get(), 0);

  zx::channel parent_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(parent_fd.release(), parent_device_handle.reset_and_get_address()));
  ASSERT_TRUE((parent_device_handle.get() != ZX_HANDLE_INVALID), "");

  uint32_t call_status;
  auto resp = fidl::WireCall<Controller>(zx::unowned_channel(parent_device_handle.get()))
                  .RunCompatibilityTests(zx::duration(zx::msec(2000)).get());
  status = resp.status();
  call_status = resp->status;

  ASSERT_OK(status);
  ASSERT_EQ(call_status, fuchsia_device_manager_CompatibilityTestStatus_ERR_UNBIND_TIMEOUT);
}
