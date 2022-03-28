// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

TEST(SimpleDriverTestRealmTest, DriversExist) {
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/root", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/leaf", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/device_group_fragment_a", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/device_group_fragment_b", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/device_group", &out));
}
