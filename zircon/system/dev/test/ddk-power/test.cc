// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/power/test/llcpp/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::DevicePowerStateInfo;
using llcpp::fuchsia::device::DevicePowerState;
using llcpp::fuchsia::device::MAX_DEVICE_POWER_STATES;
using llcpp::fuchsia::device::power::test::TestDevice;

TEST(DeviceControllerIntegrationTest, InvalidDevicePowerCaps_Less) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
  args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

  board_test::DeviceEntry dev = {};
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_POWER_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
  ASSERT_GT(parent_fd.get(), 0);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child",
      &child_fd));
  ASSERT_GT(child_fd.get(), 0);

  zx::channel child_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
  ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);

  DevicePowerStateInfo states[1];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[0].is_supported = true;
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>(1,
                       reinterpret_cast<DevicePowerStateInfo *>(states));
  auto response =
    TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned_channel(child_device_handle),
    power_states);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST(DeviceControllerIntegrationTest, InvalidDevicePowerCaps_More) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
  args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

  board_test::DeviceEntry dev = {};
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_POWER_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
  ASSERT_GT(parent_fd.get(), 0);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child",
      &child_fd));
  ASSERT_GT(child_fd.get(), 0);

  zx::channel child_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
  ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);

  DevicePowerStateInfo states[MAX_DEVICE_POWER_STATES + 1];
  for (uint8_t i = 0; i < MAX_DEVICE_POWER_STATES + 1; i++) {
    states[i].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
    states[i].is_supported = true;
  }
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>(MAX_DEVICE_POWER_STATES + 1,
    reinterpret_cast<DevicePowerStateInfo *>(states));
  auto response =
    TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned_channel(child_device_handle),
                                             power_states);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST(DeviceControllerIntegrationTest, InvalidDevicePowerCaps_MissingRequired) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
  args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

  board_test::DeviceEntry dev = {};
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_POWER_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
  ASSERT_GT(parent_fd.get(), 0);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child",
      &child_fd));
  ASSERT_GT(child_fd.get(), 0);

  zx::channel child_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
  ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);

  DevicePowerStateInfo states[MAX_DEVICE_POWER_STATES];
  for (uint8_t i = 0; i < MAX_DEVICE_POWER_STATES; i++) {
    //Missing D0 and D3COLD
    states[i].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
    states[i].is_supported = true;
  }
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>(MAX_DEVICE_POWER_STATES,
                                             reinterpret_cast<DevicePowerStateInfo *>(states));
  auto response =
    TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned_channel(child_device_handle),
                                             power_states);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST(DeviceControllerIntegrationTest, InvalidDevicePowerCaps_DuplicateCaps) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
  args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

  board_test::DeviceEntry dev = {};
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_POWER_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
  ASSERT_GT(parent_fd.get(), 0);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child",
      &child_fd));
  ASSERT_GT(child_fd.get(), 0);

  zx::channel child_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
  ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);

  DevicePowerStateInfo states[MAX_DEVICE_POWER_STATES];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  // Repeat
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>(MAX_DEVICE_POWER_STATES,
                                             reinterpret_cast<DevicePowerStateInfo *>(states));
  auto response =
    TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned_channel(child_device_handle),
                                             power_states);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST(DeviceControllerIntegrationTest, GetDevicePowerCaps_Success) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
  args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

  board_test::DeviceEntry dev = {};
  dev.vid = PDEV_VID_TEST;
  dev.pid = PDEV_PID_POWER_TEST;
  dev.did = 0;
  args.device_list.push_back(dev);

  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);
  fbl::unique_fd parent_fd, child_fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
  ASSERT_GT(parent_fd.get(), 0);

  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child",
      &child_fd));
  ASSERT_GT(child_fd.get(), 0);

  zx::channel child_device_handle;
  ASSERT_OK(
      fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
  ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);

  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>(2,
                       reinterpret_cast<DevicePowerStateInfo *>(states));
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned_channel(child_device_handle),
                                                           power_states);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}
