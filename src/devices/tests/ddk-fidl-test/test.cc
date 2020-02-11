// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/test/c/fidl.h>
#include <fuchsia/hardware/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/llcpp/coding.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <vector>

#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

void CheckTransaction(const board_test::DeviceEntry& entry, const char* driver_path,
                      const char* device_fs) {
  IsolatedDevmgr devmgr;
  zx_handle_t driver_channel;

  // Set the driver arguments.
  IsolatedDevmgr::Args args;
  args.device_list.push_back(entry);

  args.load_drivers.push_back(driver_path);
  args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);

  // Create the isolated Devmgr.
  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);

  // Wait for the driver to be created
  fbl::unique_fd fd;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), device_fs, &fd);
  ASSERT_OK(status);

  // Get a FIDL channel to the device
  status = fdio_get_service_handle(fd.get(), &driver_channel);
  ASSERT_OK(status);

  fuchsia_hardware_test_DeviceGetChannelRequest req;
  std::memset(&req, 0, sizeof(req));
  zx_txid_t first_txid = 1;
  fidl_init_txn_header(&req.hdr, first_txid, fuchsia_hardware_test_DeviceGetChannelGenOrdinal);
  uint32_t actual = 0;
  status = fidl_encode(&fuchsia_hardware_test_DeviceGetChannelRequestTable, &req, sizeof(req),
                       nullptr, 0, &actual, nullptr);
  ASSERT_OK(status);
  ASSERT_OK(zx_channel_write(driver_channel, 0, &req, sizeof(req), nullptr, 0));

  ASSERT_OK(zx_object_wait_one(driver_channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, nullptr));

  std::memset(&req, 0, sizeof(req));
  zx_txid_t second_txid = 2;
  fidl_init_txn_header(&req.hdr, second_txid, fuchsia_hardware_test_DeviceGetChannelGenOrdinal);
  status = fidl_encode(&fuchsia_hardware_test_DeviceGetChannelRequestTable, &req, sizeof(req),
                       nullptr, 0, &actual, nullptr);
  ASSERT_OK(status);
  ASSERT_OK(zx_channel_write(driver_channel, 0, &req, sizeof(req), nullptr, 0));

  // If the transaction incorrectly closes the sent handles, it will cause a policy violation.
  // Waiting for the channel to be readable once isn't enough, there is still a very small amount
  // of time before the transaction destructor runs. A second read ensures that the first
  // succeeded. If a policy violation occurs, the second read below will fail as the driver
  // channel will have been closed.
  auto msg_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  auto msg_handles = std::make_unique<zx_handle_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;

  status = zx_channel_read(driver_channel, 0, msg_bytes.get(), msg_handles.get(),
                           ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes,
                           &actual_handles);
  if (status == ZX_ERR_SHOULD_WAIT) {
    ASSERT_OK(zx_object_wait_one(driver_channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, nullptr));
  } else {
    ASSERT_OK(status);
  }

  status = zx_channel_read(driver_channel, 0, msg_bytes.get(), msg_handles.get(),
                           ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes,
                           &actual_handles);
  if (status == ZX_ERR_SHOULD_WAIT) {
    ASSERT_OK(zx_object_wait_one(driver_channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, nullptr));
  } else {
    ASSERT_OK(status);
  }
}

// Test that the transaction does not incorrectly close handles during Reply.
TEST(FidlDDKDispatcherTest, SyncTransactionHandleTest) {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "ddk-fidl");
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_DDKFIDL_TEST;
  entry.did = PDEV_DID_TEST_DDKFIDL;
  CheckTransaction(entry, "/boot/driver/fidl-llcpp-driver.so", "sys/platform/11:09:d/ddk-fidl");
}

TEST(FidlDDKDispatcherTest, AsyncTransactionHandleTest) {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "ddk-async-fidl");
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_DDKFIDL_TEST;
  entry.did = PDEV_DID_TEST_DDKASYNCFIDL;
  CheckTransaction(entry, "/boot/driver/fidl-async-llcpp-driver.so",
                   "sys/platform/11:09:15/ddk-async-fidl");
}

}  // namespace
