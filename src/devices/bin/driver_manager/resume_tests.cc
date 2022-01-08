// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

class ResumeTestCase : public MultipleDeviceTestCase {
 public:
  void ResumeTest(SystemPowerState target_state);
  void StateTest(zx_status_t resume_status, Device::State want_device_state);
};

// Verify the device transitions in and out of the resuming state.
void ResumeTestCase::StateTest(zx_status_t resume_status, Device::State want_device_state) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "", &index));

  // Mark all devices suspended.
  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  device(index)->device->set_state(Device::State::kSuspended);
  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn));

  ASSERT_NO_FATAL_FAILURE(
      sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(
      platform_bus()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Check for the resume message without replying.
  ASSERT_NO_FATAL_FAILURE(device(index)->CheckResumeReceived(SystemPowerState::kFullyOn));

  ASSERT_EQ(device(index)->device->state(), Device::State::kResuming);

  ASSERT_NO_FATAL_FAILURE(device(index)->SendResumeReply(resume_status));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(device(index)->device->state(), want_device_state);
}

// Verify the resume order is correct
void ResumeTestCase::ResumeTest(SystemPowerState target_state) {
  struct DeviceDesc {
    // Index into the device desc array below.  UINT32_MAX = platform_bus()
    const size_t parent_desc_index;
    const char* const name;
    // index for use with device()
    size_t index = 0;
    bool suspended = false;
    bool resumed = false;
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

  // Mark all devices suspended. Otherwise resume will fail
  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  for (auto& desc : devices) {
    fbl::RefPtr<Device> dev;
    size_t index = desc.index;
    if (index == UINT32_MAX) {
      continue;
    }
    dev = device(index)->device;
    if (dev->state() != Device::State::kSuspended) {
      dev->set_state(Device::State::kSuspended);
    }
  }

  ASSERT_NO_FATAL_FAILURE(DoResume(target_state));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(sys_proxy()->HasPendingMessages());
  ASSERT_NO_FATAL_FAILURE(sys_proxy()->CheckResumeReceivedAndReply(target_state, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(coordinator().sys_device()->state(), Device::State::kActive);

  ASSERT_TRUE(platform_bus()->HasPendingMessages());
  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckResumeReceivedAndReply(target_state, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(platform_bus()->device->state(), Device::State::kActive);

  size_t num_to_resume = std::size(devices);
  while (num_to_resume > 0) {
    bool made_progress = false;
    for (size_t i = 0; i < std::size(devices); ++i) {
      auto& desc = devices[i];
      if (desc.resumed) {
        continue;
      }

      if (!device(desc.index)->HasPendingMessages()) {
        continue;
      }
      ASSERT_NO_FATAL_FAILURE(device(desc.index)->CheckResumeReceivedAndReply(target_state, ZX_OK));
      coordinator_loop()->RunUntilIdle();

      size_t parent_index = devices[i].parent_desc_index;
      // Make sure all descendants of this device are not resumed yet.
      // We just need to check immediate children since this will
      // recursively enforce that property.
      for (auto& other_desc : devices) {
        if (parent_index == UINT32_MAX) {
          // Make sure platform bus is resumed.
          ASSERT_EQ(platform_bus()->device->state(), Device::State::kActive);
        } else if (other_desc.index == parent_index) {
          // Make sure parent is resumed.
          ASSERT_EQ(device(desc.index)->device->state(), Device::State::kActive);
          ASSERT_TRUE(other_desc.resumed);
        } else if (other_desc.parent_desc_index == i) {
          // if it has children, its state should be Suspended but not Active.
          ASSERT_NE(device(other_desc.index)->device->state(), Device::State::kActive);
          ASSERT_FALSE(other_desc.resumed);
        }
      }

      desc.resumed = true;
      --num_to_resume;
      made_progress = true;
    }
    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }
}

TEST_F(ResumeTestCase, FullyOnCheckOrder) {
  ASSERT_NO_FATAL_FAILURE(ResumeTest(SystemPowerState::kFullyOn));
}

TEST_F(ResumeTestCase, ResumeSuccess) {
  ASSERT_NO_FATAL_FAILURE(StateTest(ZX_OK, Device::State::kActive));
}

TEST_F(ResumeTestCase, ResumeFail) {
  ASSERT_NO_FATAL_FAILURE(StateTest(ZX_ERR_BAD_STATE, Device::State::kSuspended));
}
