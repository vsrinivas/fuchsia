// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/rtc/c/fidl.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <librtc.h>

#include <zxtest/zxtest.h>

namespace {

using driver_integration_test::IsolatedDevmgr;

// Sandboxed drivers always land at sys/platforom/..
// Below "11" is the hex value of PDEV_VID_TEST
//       "08" is the hex value of our PID - PDEV_PID_FALLBACK_RTC_TEST
//       "fallback-rtc" is the name of our driver (the way used in Bind())
constexpr char kLandingPath[] = "sys/platform/11:0c:0/fallback-rtc";
constexpr uint8_t kMetadata = PDEV_PID_FALLBACK_RTC_TEST;
const board_test::DeviceEntry kDeviceEntry = []() {
  board_test::DeviceEntry entry = {};
  strcpy(entry.name, "fallback_rtc");
  entry.vid = PDEV_VID_TEST;
  entry.pid = PDEV_PID_FALLBACK_RTC_TEST;

  entry.metadata = &kMetadata;
  entry.metadata_size = 1;

  return entry;
}();

class FallbackRTCTest : public zxtest::Test {
  void SetUp() override {
    // Create the isolated dev manager
    fbl::unique_fd rtc_fd;
    IsolatedDevmgr::Args args;

    args.device_list.push_back(kDeviceEntry);
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    // Wait for fallback-rtc to be created
    ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr_.devfs_root(), kLandingPath, &rtc_fd));

    // Get a FIDL channel to the rtc driver
    ASSERT_OK(fdio_get_service_handle(rtc_fd.release(), rtc_fdio_channel_.reset_and_get_address()));
  }

 protected:
  IsolatedDevmgr devmgr_;
  zx::channel rtc_fdio_channel_;
};

// Checks that the default time is a valid one
TEST_F(FallbackRTCTest, GetInitialDatetimeCheckValid) {
  fuchsia_hardware_rtc_Time rtc;

  ASSERT_OK(fuchsia_hardware_rtc_DeviceGet(rtc_fdio_channel_.get(), &rtc));
  ASSERT_FALSE(rtc_is_invalid(&rtc));
}

// Sets a specific date time and then verifies that the same can be red back
TEST_F(FallbackRTCTest, SetSpecificDatetimeReadBackSame) {
  int op_status;

  // set datetime
  fuchsia_hardware_rtc_Time rtcSet;
  rtcSet.year = 2019;
  rtcSet.month = 5;
  rtcSet.day = 24;
  rtcSet.hours = 19;
  rtcSet.minutes = 42;
  rtcSet.seconds = 9;
  ASSERT_OK(fuchsia_hardware_rtc_DeviceSet(rtc_fdio_channel_.get(), &rtcSet, &op_status));
  ASSERT_OK(op_status);

  // get datetime
  fuchsia_hardware_rtc_Time rtcGet;
  ASSERT_OK(fuchsia_hardware_rtc_DeviceGet(rtc_fdio_channel_.get(), &rtcGet));
  ASSERT_EQ(rtcGet.year, 2019);
  ASSERT_EQ(rtcGet.month, 5);
  ASSERT_EQ(rtcGet.day, 24);
  ASSERT_EQ(rtcGet.hours, 19);
  ASSERT_EQ(rtcGet.minutes, 42);
  ASSERT_EQ(rtcGet.seconds, 9);
}

TEST_F(FallbackRTCTest, SetInvalidDatetimeErrorAndHasNoEffect) {
  fuchsia_hardware_rtc_Time rtc;
  int op_status;

  // set datetime
  rtc.year = 2022;
  rtc.month = 6;
  rtc.day = 27;
  rtc.hours = 11;
  rtc.minutes = 2;
  rtc.seconds = 10;
  ASSERT_OK(fuchsia_hardware_rtc_DeviceSet(rtc_fdio_channel_.get(), &rtc, &op_status));
  ASSERT_OK(op_status);

  // pass invalid date
  rtc.year = 2019;
  rtc.month = 3;
  rtc.day = 32;
  rtc.hours = 17;
  rtc.minutes = 33;
  rtc.seconds = 4;
  ASSERT_OK(fuchsia_hardware_rtc_DeviceSet(rtc_fdio_channel_.get(), &rtc, &op_status));
  ASSERT_STATUS(ZX_ERR_OUT_OF_RANGE, op_status);

  // get datetime and compare with the one that was successfully set above
  ASSERT_OK(fuchsia_hardware_rtc_DeviceGet(rtc_fdio_channel_.get(), &rtc));
  ASSERT_EQ(rtc.year, 2022);
  ASSERT_EQ(rtc.month, 6);
  ASSERT_EQ(rtc.day, 27);
  ASSERT_EQ(rtc.hours, 11);
  ASSERT_EQ(rtc.minutes, 2);
  ASSERT_EQ(rtc.seconds, 10);
}

}  // namespace
