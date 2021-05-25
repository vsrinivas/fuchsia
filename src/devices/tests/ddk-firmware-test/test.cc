// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/firmware/test/llcpp/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/syscalls.h>

#include <unordered_set>

#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device_firmware_test::TestDevice;

constexpr const char TEST_FIRMWARE_PATH[] = "test-firmware";
class FirmwareTest : public zxtest::Test {
 public:
  ~FirmwareTest() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/test/ddk-firmware-test.so");

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr_);
    ASSERT_OK(status);
    fbl::unique_fd fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(),
                                                            "test/ddk-firmware-test", &fd));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_get_service_handle(fd.release(), chan_.reset_and_get_address()));
    ASSERT_NE(chan_.get(), ZX_HANDLE_INVALID);
  }

 protected:
  zx::channel chan_;
  IsolatedDevmgr devmgr_;
};

TEST_F(FirmwareTest, LoadFirmwareSync) {
  auto result = fidl::WireCall<TestDevice>(zx::unowned(chan_))
                    .LoadFirmware(fidl::StringView(TEST_FIRMWARE_PATH));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err(), "LoadFirmware failed: %s",
               zx_status_get_string(result->result.err()));
}

TEST_F(FirmwareTest, LoadNonexistantFirmwareSyncFails) {
  auto result =
      fidl::WireCall<TestDevice>(zx::unowned(chan_)).LoadFirmware(fidl::StringView("not_a_file"));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_err(), "LoadFirmware should have failed");
}

TEST_F(FirmwareTest, LoadFirmwareAsync) {
  auto result = fidl::WireCall<TestDevice>(zx::unowned(chan_))
                    .LoadFirmwareAsync(fidl::StringView(TEST_FIRMWARE_PATH));
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err(), "LoadFirmwareAsync failed: %s",
               zx_status_get_string(result->result.err()));
}

TEST_F(FirmwareTest, LoadNonexistantFirmwareAsyncFails) {
  auto result = fidl::WireCall<TestDevice>(zx::unowned(chan_))
                    .LoadFirmwareAsync(fidl::StringView("not_a_file"));
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_err(), "LoadFirmwareAsync should have failed");
}
