// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/request/c/banjo.h>

#include <src/devices/usb/lib/usb-monitor-util/include/usb-monitor-util/usb-monitor-util.h>
#include <zxtest/zxtest.h>

#include "zircon/system/ulib/zxtest/include/zxtest/zxtest.h"
namespace {
TEST(UsbMonitorUtilTest, StartStop) {
  USBMonitor test_monitor;
  test_monitor.Start();
  ASSERT_TRUE(test_monitor.Started());
  test_monitor.Stop();
  ASSERT_FALSE(test_monitor.Started());
}

TEST(UsbMonitorUtilTest, StartAddRecordStop) {
  USBMonitor test_monitor;
  test_monitor.Start();
  test_monitor.AddRecord({});
  const USBMonitorStats test_record = test_monitor.GetStats();
  ASSERT_EQ(1u, test_record.num_records, "One record should have been added");
  ASSERT_TRUE(test_monitor.Started());
  test_monitor.Stop();
  ASSERT_FALSE(test_monitor.Started());
}

}  // namespace
