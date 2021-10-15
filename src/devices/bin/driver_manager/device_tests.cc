// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/coordinator_test_utils.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/multiple_device_test.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

class DeviceChildIteratorTest : public MultipleDeviceTestCase {};

TEST_F(DeviceChildIteratorTest, Empty) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));
  coordinator_loop()->RunUntilIdle();
  ASSERT_TRUE(device(parent_index)->device->children().is_empty());
}

TEST_F(DeviceChildIteratorTest, OneChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));
  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(device(parent_index)->device, "child-device", 0, "", &child_index));
  coordinator_loop()->RunUntilIdle();
  ASSERT_FALSE(device(parent_index)->device->children().is_empty());

  for (auto& d : device(parent_index)->device->children()) {
    ASSERT_EQ(d.name(), "child-device");
  }
}

TEST_F(DeviceChildIteratorTest, MultipleChildren) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  constexpr size_t kChildren = 10;
  size_t children_index[kChildren] = {0};
  for (size_t i = 0; i < kChildren; i++) {
    fbl::String name = fbl::StringPrintf("child-device-%02zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(device(parent_index)->device, name.data(), 0, "", &children_index[i]));
  }

  size_t i = 0;
  for (auto& d : device(parent_index)->device->children()) {
    fbl::String name = fbl::StringPrintf("child-device-%02zu", i++);
    ASSERT_EQ(d.name(), name);
  }
}
