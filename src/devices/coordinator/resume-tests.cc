// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple-device-test.h"

class ResumeTestCase : public MultipleDeviceTestCase {
 public:
  void ResumeTest(SystemPowerState target_state);
  void StateTest(zx_status_t resume_status, devmgr::Device::State want_device_state);
};

// Verify the device transitions in and out of the resuming state.
void ResumeTestCase::StateTest(zx_status_t resume_status, devmgr::Device::State want_device_state) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "device", 0 /* protocol id */, "", &index));

  // Mark all devices suspened.
  coordinator_.sys_device()->set_state(devmgr::Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(devmgr::Device::State::kSuspended);
  platform_bus()->set_state(devmgr::Device::State::kSuspended);
  device(index)->device->set_state(devmgr::Device::State::kSuspended);
  ASSERT_NO_FATAL_FAILURES(DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));

  ASSERT_NO_FATAL_FAILURES(
      CheckResumeReceived(sys_proxy_remote_, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      platform_bus_remote(), SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Check for the resume message without replying.
  ASSERT_NO_FATAL_FAILURES(
      CheckResumeReceived(device(index)->remote, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));

  ASSERT_EQ(device(index)->device->state(), devmgr::Device::State::kResuming);

  ASSERT_NO_FATAL_FAILURES(SendResumeReply(device(index)->remote, resume_status));
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
    fbl::RefPtr<devmgr::Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus();
    } else {
      size_t index = devices[desc.parent_desc_index].index;
      parent = device(index)->device;
    }
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, desc.name, 0 /* protocol id */, "", &desc.index));
  }

  // Mark all devices suspened. Otherwise resume will fail
  coordinator_.sys_device()->set_state(devmgr::Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(devmgr::Device::State::kSuspended);
  platform_bus()->set_state(devmgr::Device::State::kSuspended);
  for (auto& desc : devices) {
    fbl::RefPtr<devmgr::Device> dev;
    size_t index = desc.index;
    if (index == UINT32_MAX) {
      continue;
    }
    dev = device(index)->device;
    if (dev->state() != devmgr::Device::State::kSuspended) {
      dev->set_state(devmgr::Device::State::kSuspended);
    }
  }

  ASSERT_NO_FATAL_FAILURES(DoResume(target_state));

  ASSERT_TRUE(DeviceHasPendingMessages(sys_proxy_remote_));
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(sys_proxy_remote_, target_state, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(coordinator_.sys_device()->state(), devmgr::Device::State::kResumed);

  ASSERT_TRUE(DeviceHasPendingMessages(platform_bus_remote()));
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(platform_bus_remote(), target_state, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(platform_bus()->state(), devmgr::Device::State::kResumed);

  size_t num_to_resume = fbl::count_of(devices);
  while (num_to_resume > 0) {
    bool made_progress = false;
    for (size_t i = 0; i < fbl::count_of(devices); ++i) {
      auto& desc = devices[i];
      if (desc.resumed) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }
      ASSERT_NO_FATAL_FAILURES(
          CheckResumeReceived(device(desc.index)->remote, target_state, ZX_OK));
      coordinator_loop()->RunUntilIdle();
      // Make sure all descendants of this device are not resumed yet.
      // We just need to check immediate children since this will
      // recursively enforce that property.
      for (auto& other_desc : devices) {
        if (other_desc.parent_desc_index == i) {
          // if it has children, its state should be Resumed but not Active.
          ASSERT_EQ(device(desc.index)->device->state(), devmgr::Device::State::kResumed);
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
  ASSERT_NO_FATAL_FAILURES(ResumeTest(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));
}

TEST_F(ResumeTestCase, ResumeSuccess) {
  ASSERT_NO_FATAL_FAILURES(StateTest(ZX_OK, devmgr::Device::State::kActive));
}

TEST_F(ResumeTestCase, ResumeFail) {
  ASSERT_NO_FATAL_FAILURES(StateTest(ZX_ERR_BAD_STATE, devmgr::Device::State::kSuspended));
}
