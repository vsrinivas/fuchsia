// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/manager/test/c/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

#include <vector>

#include <lib/ddk/metadata.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;

namespace {

class IsolatedDevMgrTest : public zxtest::Test {};

const uint8_t metadata1[] = {1, 2, 3, 4, 5};
const board_test::DeviceEntry kDeviceEntry1 = []() {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "metadata-test");
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_METADATA_TEST;
  entry.did = PDEV_DID_TEST_CHILD_1;
  entry.metadata_size = sizeof(metadata1);
  entry.metadata = metadata1;
  return entry;
}();

const uint8_t metadata2[] = {7, 6, 5, 4, 3, 2, 1};
const board_test::DeviceEntry kDeviceEntry2 = []() {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "metadata-test");
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_METADATA_TEST;
  entry.did = PDEV_DID_TEST_CHILD_2;
  entry.metadata_size = sizeof(metadata2);
  entry.metadata = metadata2;
  return entry;
}();

TEST_F(IsolatedDevMgrTest, MetadataOneDriverTest) {
  IsolatedDevmgr devmgr;
  zx_handle_t metadata_driver_channel;

  // Set the driver arguments.
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.device_list.push_back(kDeviceEntry1);

  // Create the isolated Devmgr.
  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);

  // Wait for Metadata-test driver to be created
  fbl::unique_fd fd;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(),
                                                         "sys/platform/11:07:2/metadata-test", &fd);
  ASSERT_OK(status);

  // Get a FIDL channel to the Metadata device
  status = fdio_get_service_handle(fd.get(), &metadata_driver_channel);
  ASSERT_OK(status);

  // Read the metadata it received.
  size_t out_size;
  std::vector<uint8_t> received_metadata(sizeof(metadata1));

  status = fuchsia_device_manager_test_MetadataGetMetadata(
      metadata_driver_channel, DEVICE_METADATA_TEST, received_metadata.data(),
      received_metadata.size(), &out_size);

  ASSERT_OK(status);
  ASSERT_EQ(out_size, sizeof(metadata1));

  for (size_t i = 0; i < received_metadata.size(); i++) {
    EXPECT_EQ(received_metadata[i], metadata1[i]);
  }
}

TEST_F(IsolatedDevMgrTest, MetadataTwoDriverTest) {
  IsolatedDevmgr devmgr;

  // Set the driver arguments.
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.device_list.push_back(kDeviceEntry1);
  args.device_list.push_back(kDeviceEntry2);

  // Create the isolated Devmgr.
  zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
  ASSERT_OK(status);

  // Wait for Metadata-test driver to be created
  fbl::unique_fd fd1;
  status = devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:07:2/metadata-test", &fd1);
  ASSERT_OK(status);
  fbl::unique_fd fd2;
  status = devmgr_integration_test::RecursiveWaitForFile(
      devmgr.devfs_root(), "sys/platform/11:07:3/metadata-test", &fd2);
  ASSERT_OK(status);

  // Get a FIDL channel to the Metadata device
  zx_handle_t metadata_driver_channel1;
  zx_handle_t metadata_driver_channel2;
  status = fdio_get_service_handle(fd1.get(), &metadata_driver_channel1);
  ASSERT_OK(status);
  status = fdio_get_service_handle(fd2.get(), &metadata_driver_channel2);
  ASSERT_OK(status);

  // Read the metadata it received.
  size_t out_size;
  std::vector<uint8_t> received_metadata(sizeof(metadata1));

  status = fuchsia_device_manager_test_MetadataGetMetadata(
      metadata_driver_channel1, DEVICE_METADATA_TEST, received_metadata.data(),
      received_metadata.size(), &out_size);

  ASSERT_OK(status);
  ASSERT_EQ(out_size, sizeof(metadata1));

  for (size_t i = 0; i < received_metadata.size(); i++) {
    EXPECT_EQ(received_metadata[i], metadata1[i]);
  }

  received_metadata.resize(sizeof(metadata2));
  status = fuchsia_device_manager_test_MetadataGetMetadata(
      metadata_driver_channel2, DEVICE_METADATA_TEST, received_metadata.data(),
      received_metadata.size(), &out_size);
  ASSERT_OK(status);
  ASSERT_EQ(out_size, sizeof(metadata2));

  for (size_t i = 0; i < received_metadata.size(); i++) {
    EXPECT_EQ(received_metadata[i], metadata2[i]);
  }
}

}  // namespace
