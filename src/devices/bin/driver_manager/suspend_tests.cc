// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

class SuspendTestCase : public MultipleDeviceTestCase {
 public:
  void SuspendTest(uint32_t flags);
  void StateTest(zx_status_t suspend_status, Device::State want_device_state);
};
TEST_F(SuspendTestCase, Poweroff) {
  ASSERT_NO_FATAL_FAILURE(SuspendTest(DEVICE_SUSPEND_FLAG_POWEROFF));
}

TEST_F(SuspendTestCase, Reboot) {
  ASSERT_NO_FATAL_FAILURE(SuspendTest(DEVICE_SUSPEND_FLAG_REBOOT));
}

TEST_F(SuspendTestCase, RebootWithFlags) {
  ASSERT_NO_FATAL_FAILURE(SuspendTest(DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER));
}

TEST_F(SuspendTestCase, Mexec) { ASSERT_NO_FATAL_FAILURE(SuspendTest(DEVICE_SUSPEND_FLAG_MEXEC)); }

TEST_F(SuspendTestCase, SuspendToRam) {
  ASSERT_NO_FATAL_FAILURE(SuspendTest(DEVICE_SUSPEND_FLAG_SUSPEND_RAM));
}

// Verify the suspend order is correct
void SuspendTestCase::SuspendTest(uint32_t flags) {
  struct DeviceDesc {
    // Index into the device desc array below.  UINT32_MAX = platform_bus()
    const size_t parent_desc_index;
    const char* const name;
    // index for use with device()
    size_t index = 0;
    bool suspended = false;
  };
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"}, {UINT32_MAX, "root_child2"}, {0, "root_child1_1"},
      {0, "root_child1_2"},        {2, "root_child1_1_1"},      {1, "root_child2_1"},
  };
  for (auto& desc : devices) {
    fbl::RefPtr<Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus()->device;
    } else {
      size_t index = devices[desc.parent_desc_index].index;
      parent = device(index)->device;
    }
    ASSERT_NO_FATAL_FAILURE(AddDevice(parent, desc.name, 0 /* protocol id */, "", &desc.index));
  }

  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  size_t num_to_suspend = std::size(devices);
  while (num_to_suspend > 0) {
    // Check that platform bus is not suspended yet.
    ASSERT_FALSE(platform_bus()->HasPendingMessages());

    bool made_progress = false;
    // Since the table of devices above is topologically sorted (i.e.
    // any child is below its parent), this loop should always be able
    // to catch a parent receiving a suspend message before its child.
    for (size_t i = 0; i < std::size(devices); ++i) {
      auto& desc = devices[i];
      if (desc.suspended) {
        continue;
      }

      if (!device(desc.index)->HasPendingMessages()) {
        continue;
      }

      ASSERT_NO_FATAL_FAILURE(device(desc.index)->CheckSuspendReceivedAndReply(flags, ZX_OK));

      // Make sure all descendants of this device are already suspended.
      // We just need to check immediate children since this will
      // recursively enforce that property.
      for (auto& other_desc : devices) {
        if (other_desc.parent_desc_index == i) {
          ASSERT_TRUE(other_desc.suspended);
        }
      }

      desc.suspended = true;
      --num_to_suspend;
      made_progress = true;
    }

    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));
}

TEST_F(SuspendTestCase, SuspendSuccess) {
  ASSERT_NO_FATAL_FAILURE(StateTest(ZX_OK, Device::State::kSuspended));
}

TEST_F(SuspendTestCase, SuspendFail) {
  ASSERT_NO_FATAL_FAILURE(StateTest(ZX_ERR_BAD_STATE, Device::State::kActive));
}

// Verify the device transitions in and out of the suspending state.
void SuspendTestCase::StateTest(zx_status_t suspend_status, Device::State want_device_state) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "", &index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  // Check for the suspend message without replying.
  ASSERT_NO_FATAL_FAILURE(device(index)->CheckSuspendReceived(flags));

  ASSERT_EQ(device(index)->device->state(), Device::State::kSuspending);

  ASSERT_NO_FATAL_FAILURE(device(index)->SendSuspendReply(suspend_status));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(device(index)->device->state(), want_device_state);
}
