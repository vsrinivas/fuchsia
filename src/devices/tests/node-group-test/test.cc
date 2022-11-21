// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-watcher/cpp/device-watcher.h>

#include <gtest/gtest.h>

TEST(SimpleDriverTestRealmTest, DriversExist) {
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/root", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/leaf", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/node_group_fragment_a_1", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/node_group_fragment_b_1", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/node_group_fragment_a_2", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/node_group_fragment_b_2", &out));
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile("/dev/sys/test/node_group_fragment_c_2", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(
                       "/dev/sys/test/node_group_fragment_a_1/node_group_driver/node_group", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(
                       "/dev/sys/test/node_group_fragment_a_2/node_group_driver/node_group", &out));
}
