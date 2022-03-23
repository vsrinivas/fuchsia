// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

using devmgr_integration_test::IsolatedDevmgr;

TEST(MetadataTest, RunTests) {
  const char kDriver[] = "/boot/driver/ddk-metadata-test-driver.so";
  auto args = IsolatedDevmgr::DefaultArgs();

  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  zx::channel sys_chan;
  {
    fbl::unique_fd fd;
    ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
    ASSERT_OK(fdio_get_service_handle(fd.release(), sys_chan.reset_and_get_address()));
  }
  fidl::WireSyncClient<fuchsia_device::Controller> sys_dev(std::move(sys_chan));

  auto result = sys_dev->Bind(fidl::StringView{kDriver});
  ASSERT_OK(result.status());
  // The driver will run its tests in its bind routine, and return ZX_OK on success.
  ASSERT_FALSE(result->result.is_err());
}

// Test the Metadata struct helper:
TEST(MetadataTest, GetMetadataStructTest) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  constexpr size_t kMetadataType = 5;
  struct MetadataType {
    int data[4];
    float data1;
  };
  MetadataType metadata_source;
  parent->SetMetadata(kMetadataType, &metadata_source, sizeof(metadata_source));

  auto metadata_result = ddk::GetMetadata<MetadataType>(parent.get(), kMetadataType);

  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_BYTES_EQ(metadata_result.value().get(), &metadata_source, sizeof(MetadataType));
}

// Test the Metadata Array helper:
TEST(MetadataTest, MetadataArrayTests) {
  auto parent = MockDevice::FakeRootParent();  // Hold on to the parent during the test.
  constexpr size_t kMetadataType = 5;
  struct MetadataType {
    int data[4];
    float data1;
  };

  constexpr size_t kMetadataArrayLen = 5;
  MetadataType metadata_source[kMetadataArrayLen];
  parent->SetMetadata(kMetadataType, &metadata_source, sizeof(metadata_source));

  auto metadata_result = ddk::GetMetadataArray<MetadataType>(parent.get(), kMetadataType);

  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_EQ(metadata_result->size(), kMetadataArrayLen);
  for (size_t i = 0; i < metadata_result->size(); ++i) {
    ASSERT_BYTES_EQ(&metadata_result.value()[i], &metadata_source[i], sizeof(MetadataType));
  }
}

// Simple device to test DeviceAddArgs
class TestDevice : public ddk::Device<TestDevice> {
 public:
  TestDevice(zx_device_t* parent) : ddk::Device<TestDevice>(parent) {}
  void DdkRelease() {
    // DdkRelease must delete this before it returns.
    delete this;
  }
};

TEST(MetadataTest, DeviceAddArgTests) {
  // We use two parents here because if we added the metadata to the normal parent,
  // it would be accessible through the child by recursion.
  auto parent = MockDevice::FakeRootParent();           // Hold on to the parent during the test.
  auto metadata_parent = MockDevice::FakeRootParent();  // This will actually have the metadata
  constexpr size_t kMetadataType = 5;
  struct MetadataType {
    int data[4];
    float data1;
  };

  constexpr size_t kMetadataArrayLen = 5;
  MetadataType metadata_source[kMetadataArrayLen];
  metadata_parent->SetMetadata(kMetadataType, &metadata_source, sizeof(metadata_source));

  // Bind a new device:
  auto dev = std::make_unique<TestDevice>(parent.get());
  // Now, add the child, but allow it to get the metadata from the metadata_parent:
  auto args = ddk::DeviceAddArgs("dut").forward_metadata(metadata_parent.get(), kMetadataType);
  ASSERT_EQ(args.get().metadata_count, 1);
  ASSERT_EQ(args.get().metadata_list[0].type, kMetadataType);

  ASSERT_OK(dev->DdkAdd(args));
  auto dev_ptr = dev.release();

  // Now the metadata should be available in the device:
  auto metadata_result = ddk::GetMetadataArray<MetadataType>(dev_ptr->zxdev(), kMetadataType);

  ASSERT_TRUE(metadata_result.is_ok());
  ASSERT_EQ(metadata_result->size(), kMetadataArrayLen);
  for (size_t i = 0; i < metadata_result->size(); ++i) {
    ASSERT_BYTES_EQ(&metadata_result.value()[i], &metadata_source[i], sizeof(MetadataType));
  }
}

}  // namespace
