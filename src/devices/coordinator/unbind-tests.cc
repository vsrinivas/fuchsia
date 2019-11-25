// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "multiple-device-test.h"

class UnbindTestCase : public MultipleDeviceTestCase {
 public:
  // The expected action to receive. This is required as device_remove does not call unbind
  // on the initial device.
  enum class Action {
    kNone,
    kRemove,
    kUnbind,
  };

  struct DeviceDesc {
    // Index into the device desc array below.  UINT32_MAX = platform_bus()
    const size_t parent_desc_index;
    const char* const name;
    Action want_action = Action::kNone;
    // If non-null, will be run after receiving the Remove / Unbind request,
    // but before replying.
    std::function<void()> unbind_op = nullptr;
    // index for use with device()
    size_t index = 0;
    bool removed = false;
    bool unbound = false;
  };
  // |target_device_index| is the index of the device in the |devices| array to
  // schedule removal of.
  // If |unbind_children_only| is true, it will skip removal of the target device.
  void UnbindTest(DeviceDesc devices[], size_t num_devices, size_t target_device_index,
                  bool unbind_children_only = false, bool unbind_target_device = false);
};

TEST_F(UnbindTestCase, UnbindLeaf) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"}, {UINT32_MAX, "root_child2"},
      {0, "root_child1_1"},        {0, "root_child1_2"},
      {2, "root_child1_1_1"},      {1, "root_child2_1", Action::kRemove},
  };
  // Only remove root_child2_1.
  size_t index_to_remove = 5;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

TEST_F(UnbindTestCase, UnbindMultipleChildren) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kRemove}, {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},        {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},      {1, "root_child2_1"},
  };
  // Remove root_child1 and all its children.
  size_t index_to_remove = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

// This tests the removal of a child device in unbind. e.g.
//
// void MyDevice::Unbind() {
//   child->DdkRemove();
//   DdkRemove();
// }
TEST_F(UnbindTestCase, UnbindWithRemoveOp) {
  // Remove root_child1 and all its children.
  size_t index_to_remove = 0;
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kRemove},
      {0, "root_child1_1", Action::kUnbind},
      {1, "root_child1_1_1", Action::kRemove},
      {2, "root_child1_1_1_1", Action::kUnbind},
  };

  // We will schedule child device 1_1_1's removal in device 1_1's unbind hook.
  auto unbind_op = [&] {
    ASSERT_NO_FATAL_FAILURES(
        coordinator_.ScheduleDevhostRequestedRemove(device(devices[2].index)->device));
  };
  devices[1].unbind_op = unbind_op;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove));
}

TEST_F(UnbindTestCase, UnbindChildrenOnly) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1"},  // Unbinding children of this device.
      {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},
      {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},
      {1, "root_child2_1"},
  };
  // Remove the children of root_child1.
  size_t target_device_index = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), target_device_index,
                                      true /* unbind_children_only */));
}

TEST_F(UnbindTestCase, UnbindSelf) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kUnbind},  // Require unbinding of the target device.
      {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},
      {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},
      {1, "root_child2_1"},
  };
  // Unbind root_child1.
  size_t index_to_remove = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, fbl::count_of(devices), index_to_remove,
                                      false /* unbind_children_only */,
                                      true /* unbind_target_device */));
}

void UnbindTestCase::UnbindTest(DeviceDesc devices[], size_t num_devices,
                                size_t target_device_index, bool unbind_children_only,
                                bool unbind_target_device) {
  size_t num_to_remove = 0;
  size_t num_to_unbind = 0;
  for (size_t i = 0; i < num_devices; i++) {
    auto& desc = devices[i];
    fbl::RefPtr<devmgr::Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus();
    } else {
      size_t index = devices[desc.parent_desc_index].index;
      parent = device(index)->device;
    }
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, desc.name, 0 /* protocol id */, "", &desc.index));
    if (desc.want_action == Action::kUnbind) {
      num_to_unbind++;
      num_to_remove++;
    } else if (desc.want_action == Action::kRemove) {
      num_to_remove++;
    }
  }

  auto& desc = devices[target_device_index];
  if (unbind_children_only) {
    // Skip removal of the target device.
    ASSERT_NO_FATAL_FAILURES(
        coordinator_.ScheduleDevhostRequestedUnbindChildren(device(desc.index)->device));
  } else {
    ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleDevhostRequestedRemove(device(desc.index)->device,
                                                                         unbind_target_device));
  }
  coordinator_loop()->RunUntilIdle();

  while (num_to_unbind > 0) {
    bool made_progress = false;
    // Currently devices are unbound from the ancestor first.
    // Always check from leaf device upwards, so we ensure no child
    // is unbound before its parent.
    // To avoid overflow, check the counter before it is decremented.
    for (size_t i = num_devices; i-- > 0;) {
      auto& desc = devices[i];
      if (desc.unbound) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }
      ASSERT_NE(desc.want_action, Action::kNone);
      zx_txid_t txid;
      if (desc.want_action == Action::kUnbind) {
        ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(device(desc.index)->controller_remote, &txid));
        if (desc.unbind_op) {
          desc.unbind_op();
        }
        ASSERT_NO_FATAL_FAILURES(SendUnbindReply(device(desc.index)->controller_remote, txid));
        desc.unbound = true;
      }
      // Check if the parent is expected to have been unbound already.
      if (desc.parent_desc_index != UINT32_MAX) {
        auto parent_desc = devices[desc.parent_desc_index];
        if (parent_desc.want_action == Action::kUnbind) {
          ASSERT_TRUE(parent_desc.unbound);
        }
      }

      --num_to_unbind;
      made_progress = true;
    }
    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  // Now check that we receive the removals in the expected order, leaf first.
  while (num_to_remove > 0) {
    bool made_progress = false;
    for (size_t i = 0; i < num_devices; ++i) {
      auto& desc = devices[i];
      if (desc.removed) {
        continue;
      }

      if (!DeviceHasPendingMessages(desc.index)) {
        continue;
      }

      ASSERT_NE(desc.want_action, Action::kNone);
      ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(desc.index)->controller_remote));

      // Check that all our children have already been removed.
      for (size_t j = 0; j < num_devices; ++j) {
        auto& other_desc = devices[j];
        if (other_desc.parent_desc_index == i) {
          ASSERT_TRUE(other_desc.removed);
        }
      }

      desc.removed = true;
      --num_to_remove;
      made_progress = true;
    }

    // Make sure we're not stuck waiting
    ASSERT_TRUE(made_progress);
    coordinator_loop()->RunUntilIdle();
  }

  for (size_t i = 0; i < num_devices; i++) {
    auto& desc = devices[i];
    ASSERT_NULL(device(desc.index)->device->GetActiveUnbind());
    ASSERT_NULL(device(desc.index)->device->GetActiveRemove());
  }
}
TEST_F(UnbindTestCase, UnbindSysDevice) {
  // Since the sys device is immortal, only its children will be unbound.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_coordinator_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(platform_bus_controller_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_coordinator_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(platform_bus_controller_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(sys_proxy_controller_remote_));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator_.sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator_.sys_device()->GetActiveRemove());
}

TEST_F(UnbindTestCase, UnbindWhileRemovingProxy) {
  // The unbind task should complete immediately.
  // The remove task is blocked on the platform bus remove task completing.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()->proxy()));

  // Since the sys device is immortal, only its children will be unbound.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(coordinator_.sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_coordinator_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(platform_bus_controller_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(sys_proxy_coordinator_remote_));

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(platform_bus_controller_remote()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(sys_proxy_controller_remote_));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator_.sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator_.sys_device()->GetActiveRemove());
}

// If this test fails, you will likely see log errors when removing devices.
TEST_F(UnbindTestCase, NumRemovals) {
  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // Make sure the coordinator device does not detect the devhost's remote channel closing,
  // otherwise it will try to remove an already dead device and we will get a log error.
  child_device->coordinator_remote.reset();
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(child_device->device->num_removal_attempts(), 1);
}

TEST_F(UnbindTestCase, AddDuringParentUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  zx_txid_t txid;
  // Don't reply to the request until we add the device.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceived(parent_device->controller_remote, &txid));

  // Adding a child device to an unbinding parent should fail.
  fbl::RefPtr<devmgr::Device> child;

  zx::channel coordinator_local, coordinator_remote;
  zx_status_t status = zx::channel::create(0, &coordinator_local, &coordinator_remote);
  ASSERT_OK(status);

  zx::channel controller_local, controller_remote;
  status = zx::channel::create(0, &controller_local, &controller_remote);
  ASSERT_OK(status);

  fbl::RefPtr<devmgr::Device> device;
  status = coordinator_.AddDevice(parent_device->device, std::move(controller_local),
                                  std::move(coordinator_local), nullptr /* props_data */,
                                  0 /* props_count */, "child", 0 /* protocol_id */,
                                  nullptr /* driver_path */, nullptr /* args */,
                                  false /* invisible */, false /* do_init */,
                                  zx::channel() /* client_remote */, &child);
  ASSERT_NOT_OK(status);
  coordinator_loop()->RunUntilIdle();

  // Complete the original parent unbind.
  ASSERT_NO_FATAL_FAILURES(SendRemoveReply(parent_device->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();
}
TEST_F(UnbindTestCase, TwoConcurrentRemovals) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  // Schedule concurrent removals.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->controller_remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, ManyConcurrentRemovals) {
  size_t num_devices = 100;
  size_t idx_map[num_devices];

  for (size_t i = 0; i < num_devices; i++) {
    auto parent = i == 0 ? platform_bus() : device(idx_map[i - 1])->device;
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, "child", 0 /* protocol id */, "", &idx_map[i]));
  }

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(idx_map[i])->device));
  }

  coordinator_loop()->RunUntilIdle();

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(
        CheckRemoveReceivedAndReply(device(idx_map[num_devices - i - 1])->controller_remote));
    coordinator_loop()->RunUntilIdle();
  }
}
TEST_F(UnbindTestCase, ForcedRemovalDuringUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  zx_txid_t txid;
  // Don't reply to the unbind request.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(child_device->controller_remote, &txid));

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->controller_remote = zx::channel();
  parent_device->coordinator_remote = zx::channel();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(devmgr::Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(devmgr::Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());
}
TEST_F(UnbindTestCase, ForcedRemovalDuringRemove) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(child_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the remove request.
  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceived(child_device->controller_remote, &txid));

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->controller_remote = zx::channel();
  parent_device->coordinator_remote = zx::channel();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(devmgr::Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(devmgr::Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(child_device->device->GetActiveRemove());
}

TEST_F(UnbindTestCase, RemoveParentWhileRemovingChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  // Add a grandchild so that the child's remove task does not begin running after the
  // child's unbind task completes.
  size_t grandchild_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(child_device->device, "grandchild", 0 /* protocol id */, "", &grandchild_index));

  auto* grandchild_device = device(grandchild_index);

  // Start removing the child. Since we are not requesting an unbind
  // the unbind task will complete immediately. The remove task will be waiting
  // on the grandchild's remove to complete.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  // Start removing the parent.
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(grandchild_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(grandchild_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->controller_remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, RemoveParentAndChildSimultaneously) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(
      coordinator_.ScheduleDevhostRequestedRemove(parent_device->device, false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // At the same time, have the child try to remove itself.
  ASSERT_NO_FATAL_FAILURES(
      coordinator_.ScheduleDevhostRequestedRemove(child_device->device, false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  zx_txid_t txid;
  // The child device will not reply, as it already called device_remove previously.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(child_device->controller_remote, &txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(child_device->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(parent_device->controller_remote));
  coordinator_loop()->RunUntilIdle();
}
