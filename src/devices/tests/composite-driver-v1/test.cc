// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

TEST(SimpleDriverTestRealmTest, DriversExist) {
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/child_a", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/child_b", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/child_c", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/composite_driver_v1", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/composite_driver_v1/composite_child", &out));
}
