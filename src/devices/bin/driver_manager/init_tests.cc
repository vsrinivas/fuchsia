// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

class InitTestCase : public MultipleDeviceTestCase {};

TEST_F(InitTestCase, Init) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckInitReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

// Tests that a device will not be unbound until init completes.
TEST_F(InitTestCase, InitThenUnbind) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckInitReceived());

  ASSERT_NO_FATAL_FAILURE(coordinator().device_manager()->ScheduleDriverHostRequestedRemove(
      device(index)->device, true /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // We should not get the unbind request until we reply to the init.
  ASSERT_FALSE(device(index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(index)->SendInitReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(index)->device->state());
}

TEST_F(InitTestCase, InitThenSuspend) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckInitReceived());

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  coordinator_loop()->RunUntilIdle();

  // We should not get the suspend request until we reply to the init.
  ASSERT_FALSE(device(index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(index)->SendInitReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));

  ASSERT_EQ(Device::State::kSuspended, device(index)->device->state());
}

TEST_F(InitTestCase, ForcedRemovalDuringInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    true /* always_init */, zx::vmo() /* inspect */, &index));

  auto* test_device = device(index);

  // Don't reply to the init request.
  ASSERT_NO_FATAL_FAILURE(test_device->CheckInitReceived());

  // Close the device's channel to trigger a forced removal.
  test_device->controller_server.reset();
  test_device->coordinator_client.reset();
  coordinator_loop()->RunUntilIdle();

  // Check the device is dead and has no pending init task.
  ASSERT_EQ(Device::State::kDead, test_device->device->state());
  ASSERT_NULL(test_device->device->GetActiveInit());
  ASSERT_NO_FATAL_FAILURE(test_device->SendInitReply());
}

// Tests that a device is unbound if init fails.
TEST_F(InitTestCase, FailedInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    true /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckInitReceivedAndReply(ZX_ERR_NO_MEMORY));
  coordinator_loop()->RunUntilIdle();

  // Init failed, so device should not be visible.
  ASSERT_FALSE(device(index)->device->is_visible());

  // Unbind should be scheduled.
  ASSERT_NO_FATAL_FAILURE(device(index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(index)->device->state());
}

// Tests that a child init task will not run until the parent's init task completes.
TEST_F(InitTestCase, InitParentThenChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(
      platform_bus()->device, "parent-device", 0 /* protocol id */, "", true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &parent_index));

  // Don't reply to init yet.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckInitReceived());
  coordinator_loop()->RunUntilIdle();

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(
      device(parent_index)->device, "child-device", 0 /* protocol id */, "", true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &child_index));

  // Child init should not run until parent init task completes.
  ASSERT_FALSE(device(child_index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->SendInitReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckInitReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(InitTestCase, InitParentFail) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(
      platform_bus()->device, "parent-device", 0 /* protocol id */, "", true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &parent_index));

  // Don't reply to init yet.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckInitReceived());
  coordinator_loop()->RunUntilIdle();

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(
      device(parent_index)->device, "child-device", 0 /* protocol id */, "", true /* has_init */,
      false /* reply_to_init */, true /* always_init */, zx::vmo() /* inspect */, &child_index));

  ASSERT_FALSE(device(child_index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->SendInitReply(ZX_ERR_NO_MEMORY));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckInitReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // The parent and child devices should be removed after a failed init.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(Device::State::kDead, device(parent_index)->device->state());
  ASSERT_EQ(Device::State::kDead, device(child_index)->device->state());
}

// TODO(fxbug.dev/43370): these tests can be removed once init tasks can be enabled for all devices.
TEST_F(InitTestCase, LegacyNoInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    false /* has_init */, false /* reply_to_init */,
                                    false /* always_init */, zx::vmo() /* inspect */, &index));
  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}

TEST_F(InitTestCase, LegacyInit) {
  size_t index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "device", 0 /* protocol id */, "",
                                    true /* has_init */, false /* reply_to_init */,
                                    false /* always_init */, zx::vmo() /* inspect */, &index));

  ASSERT_FALSE(device(index)->device->is_visible());

  ASSERT_NO_FATAL_FAILURE(device(index)->CheckInitReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_TRUE(device(index)->device->is_visible());
  ASSERT_EQ(Device::State::kActive, device(index)->device->state());
}
