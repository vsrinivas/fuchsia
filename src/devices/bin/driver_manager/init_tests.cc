// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

class InitTestCase : public MultipleDeviceTestCase {};

TEST_F(InitTestCase, Init) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests adding a device as invisible, which also has implemented an init hook.
TEST_F(InitTestCase, InvisibleAndInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", true /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests adding a device as invisible, which has not also implemented an init hook.
// We will reply to the default init before calling MakeVisible.
TEST_F(InitTestCase, MakeVisibleThenDefaultInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", true /* invisible */, false /* has_init */,
      true /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  // Not visible until we call MakeVisible.
  ASSERT_FALSE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());

  coordinator().MakeVisible(device(index)->device);
  ASSERT_TRUE(device(index)->device->is_visible());
}

// Tests adding a device as invisible, which has not also implemented an init hook.
// We will call MakeVisible before replying to the default init.
TEST_F(InitTestCase, DefaultInitThenMakeVisible) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", true /* invisible */, false /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kInitializing, device(index)->device->state());

  // Not visible until the init hook completes.
  coordinator().MakeVisible(device(index)->device);
  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(index)->controller_remote, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests that a device will not be unbound until init completes.
TEST_F(InitTestCase, InitThenUnbind) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckInitReceived(device(index)->controller_remote, &txid));

  ASSERT_NO_FATAL_FAILURES(
      coordinator().ScheduleDriverHostRequestedRemove(device(index)->device, true /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // We should not get the unbind request until we reply to the init.
  ASSERT_FALSE(DeviceHasPendingMessages(device(index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(SendInitReply(device(index)->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(index)->device->state());
}

TEST_F(InitTestCase, InitThenSuspend) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckInitReceived(device(index)->controller_remote, &txid));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  coordinator_loop()->RunUntilIdle();

  // We should not get the suspend request until we reply to the init.
  ASSERT_FALSE(DeviceHasPendingMessages(device(index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(SendInitReply(device(index)->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(device(index)->controller_remote, flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), flags, ZX_OK));

  ASSERT_EQ(Device::State::kSuspended, device(index)->device->state());
}

TEST_F(InitTestCase, ForcedRemovalDuringInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  auto* test_device = device(index);

  zx_txid_t txid;
  // Don't reply to the init request.
  ASSERT_NO_FATAL_FAILURES(CheckInitReceived(test_device->controller_remote, &txid));

  // Close the device's channel to trigger a forced removal.
  test_device->controller_remote = zx::channel();
  test_device->coordinator_remote = zx::channel();
  coordinator_loop()->RunUntilIdle();

  // Check the device is dead and has no pending init task.
  ASSERT_EQ(Device::State::kDead, test_device->device->state());
  ASSERT_NULL(test_device->device->GetActiveInit());
}

// Tests that a device is unbound if init fails.
TEST_F(InitTestCase, FailedInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(
      CheckInitReceivedAndReply(device(index)->controller_remote, ZX_ERR_NO_MEMORY));
  coordinator_loop()->RunUntilIdle();

  // Init failed, so device should not be visible.
  ASSERT_FALSE(device(index)->device->is_visible());

  // Unbind should be scheduled.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(index)->device->state());
}

// Tests that a child init task will not run until the parent's init task completes.
TEST_F(InitTestCase, InitParentThenChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "",
                                     false /* invisible */, true /* has_init */,
                                     false /* reply_to_init */, true /* always_init */,
                                     zx::vmo() /* inspect */, &parent_index));

  // Don't reply to init yet.
  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckInitReceived(device(parent_index)->controller_remote, &txid));
  coordinator_loop()->RunUntilIdle();

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(device(parent_index)->device, "child-device", 0 /* protocol id */, "",
                false /* invisible */, true /* has_init */, false /* reply_to_init */,
                true /* always_init */, zx::vmo() /* inspect */, &child_index));

  // Child init should not run until parent init task completes.
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(SendInitReply(device(parent_index)->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(InitTestCase, InitParentFail) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "",
                                     false /* invisible */, true /* has_init */,
                                     false /* reply_to_init */, true /* always_init */,
                                     zx::vmo() /* inspect */, &parent_index));

  // Don't reply to init yet.
  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckInitReceived(device(parent_index)->controller_remote, &txid));
  coordinator_loop()->RunUntilIdle();

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(device(parent_index)->device, "child-device", 0 /* protocol id */, "",
                false /* invisible */, true /* has_init */, false /* reply_to_init */,
                true /* always_init */, zx::vmo() /* inspect */, &child_index));

  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(
      SendInitReply(device(parent_index)->controller_remote, txid, ZX_ERR_NO_MEMORY));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // The parent and child devices should be removed after a failed init.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(parent_index)->device->state());
  ASSERT_EQ(Device::State::kDead, device(child_index)->device->state());
}

// TODO(fxbug.dev/43370): these tests can be removed once init tasks can be enabled for all devices.
TEST_F(InitTestCase, LegacyNoInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "device", 0 /* protocol id */, "",
                                     false /* invisible */, false /* has_init */,
                                     false /* reply_to_init */, false /* always_init */,
                                     zx::vmo() /* inspect */, &index));
  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

TEST_F(InitTestCase, LegacyInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", false /* invisible */, true /* has_init */,
      false /* reply_to_init */, false /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests adding a device as invisible, which also has implemented an init hook.
TEST_F(InitTestCase, LegacyInvisibleAndInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", true /* invisible */, true /* has_init */,
      false /* reply_to_init */, false /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURES(CheckInitReceivedAndReply(device(index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests adding a device as invisible, which has not also implemented an init hook.
TEST_F(InitTestCase, LegacyInvisibleNoInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(
      platform_bus(), "device", 0 /* protocol id */, "", true /* invisible */, false /* has_init */,
      false /* reply_to_init */, false /* always_init */, zx::vmo() /* inspect */, &index));

  // Not visible until we call MakeVisible.
  ASSERT_FALSE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());

  coordinator().MakeVisible(device(index)->device);
  ASSERT_TRUE(device(index)->device->is_visible());
}
