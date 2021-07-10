// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "multiple_device_test.h"

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
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, std::size(devices), index_to_remove));
}

TEST_F(UnbindTestCase, UnbindMultipleChildren) {
  DeviceDesc devices[] = {
      {UINT32_MAX, "root_child1", Action::kRemove}, {UINT32_MAX, "root_child2"},
      {0, "root_child1_1", Action::kUnbind},        {0, "root_child1_2", Action::kUnbind},
      {2, "root_child1_1_1", Action::kUnbind},      {1, "root_child2_1"},
  };
  // Remove root_child1 and all its children.
  size_t index_to_remove = 0;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, std::size(devices), index_to_remove));
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
        coordinator().ScheduleDriverHostRequestedRemove(device(devices[2].index)->device));
  };
  devices[1].unbind_op = unbind_op;
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, std::size(devices), index_to_remove));
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
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, std::size(devices), target_device_index,
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
  ASSERT_NO_FATAL_FAILURES(UnbindTest(devices, std::size(devices), index_to_remove,
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
    fbl::RefPtr<Device> parent;
    if (desc.parent_desc_index == UINT32_MAX) {
      parent = platform_bus()->device;
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
        coordinator().ScheduleDriverHostRequestedUnbindChildren(device(desc.index)->device));
  } else {
    ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleDriverHostRequestedRemove(
        device(desc.index)->device, unbind_target_device));
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

      if (!device(desc.index)->HasPendingMessages()) {
        continue;
      }
      ASSERT_NE(desc.want_action, Action::kNone);
      if (desc.want_action == Action::kUnbind) {
        ASSERT_NO_FATAL_FAILURES(device(desc.index)->CheckUnbindReceived());
        if (desc.unbind_op) {
          desc.unbind_op();
        }
        ASSERT_NO_FATAL_FAILURES(device(desc.index)->SendUnbindReply());
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

      if (!device(desc.index)->HasPendingMessages()) {
        continue;
      }

      ASSERT_NE(desc.want_action, Action::kNone);
      ASSERT_NO_FATAL_FAILURES(device(desc.index)->CheckRemoveReceivedAndReply());

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
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(coordinator().sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(sys_proxy()->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(sys_proxy()->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(sys_proxy()->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator().sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator().sys_device()->GetActiveRemove());
}

TEST_F(UnbindTestCase, UnbindWhileRemovingProxy) {
  // The unbind task should complete immediately.
  // The remove task is blocked on the platform bus remove task completing.
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(coordinator().sys_device()->proxy()));

  // Since the sys device is immortal, only its children will be unbound.
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(coordinator().sys_device()));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(sys_proxy()->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(sys_proxy()->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(sys_proxy()->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NULL(coordinator().sys_device()->GetActiveUnbind());
  ASSERT_NULL(coordinator().sys_device()->GetActiveRemove());
}

// If this test fails, you will likely see log errors when removing devices.
TEST_F(UnbindTestCase, NumRemovals) {
  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // Make sure the coordinator device does not detect the driver_host's remote channel closing,
  // otherwise it will try to remove an already dead device and we will get a log error.
  child_device->coordinator_client.reset();
  coordinator_loop()->RunUntilIdle();

  ASSERT_EQ(child_device->device->num_removal_attempts(), 1);
}

TEST_F(UnbindTestCase, AddDuringParentUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the request until we add the device.
  ASSERT_NO_FATAL_FAILURES(parent_device->CheckRemoveReceived());

  // Adding a child device to an unbinding parent should fail.
  fbl::RefPtr<Device> child;

  auto controller_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::DeviceController>();
  ASSERT_OK(controller_endpoints.status_value());

  auto coordinator_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Coordinator>();
  ASSERT_OK(coordinator_endpoints.status_value());

  fbl::RefPtr<Device> device;
  auto status = coordinator().AddDevice(
      parent_device->device, std::move(controller_endpoints->client),
      std::move(coordinator_endpoints->server), nullptr /* props_data */, 0 /* props_count */,
      nullptr /* str_props_data */, 0 /* str_props_count */, "child", 0 /* protocol_id */,
      {} /* driver_path */, {} /* args */, false /* invisible */, false /* skip_autobind */,
      false /* has_init */, true /* always_init */, zx::vmo() /*inspect*/,
      zx::channel() /* client_remote */, &child);
  ASSERT_NOT_OK(status);
  coordinator_loop()->RunUntilIdle();

  // Complete the original parent unbind.
  ASSERT_NO_FATAL_FAILURES(parent_device->SendRemoveReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, TwoConcurrentRemovals) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  // Schedule concurrent removals.
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(parent_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, ManyConcurrentRemovals) {
  size_t num_devices = 100;
  size_t idx_map[num_devices];

  for (size_t i = 0; i < num_devices; i++) {
    auto parent = i == 0 ? platform_bus()->device : device(idx_map[i - 1])->device;
    ASSERT_NO_FATAL_FAILURES(AddDevice(parent, "child", 0 /* protocol id */, "", &idx_map[i]));
  }

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(device(idx_map[i])->device));
  }

  coordinator_loop()->RunUntilIdle();

  for (size_t i = 0; i < num_devices; i++) {
    ASSERT_NO_FATAL_FAILURES(device(idx_map[num_devices - i - 1])->CheckRemoveReceivedAndReply());
    coordinator_loop()->RunUntilIdle();
  }
}
TEST_F(UnbindTestCase, ForcedRemovalDuringUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the unbind request.
  ASSERT_NO_FATAL_FAILURES(child_device->CheckUnbindReceived());

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->controller_server.reset();
  parent_device->coordinator_client.reset();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_NO_FATAL_FAILURES(child_device->SendUnbindReply());
}
TEST_F(UnbindTestCase, ForcedRemovalDuringRemove) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // Don't reply to the remove request.
  ASSERT_NO_FATAL_FAILURES(child_device->CheckRemoveReceived());

  // Close the parent device's channel to trigger a forced removal of the parent and child.
  parent_device->controller_server.reset();
  parent_device->coordinator_client.reset();
  coordinator_loop()->RunUntilIdle();

  // Check that both devices are dead and have no pending unbind or remove tasks.
  ASSERT_EQ(Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(child_device->device->GetActiveRemove());

  ASSERT_NO_FATAL_FAILURES(child_device->SendRemoveReply());
}

TEST_F(UnbindTestCase, RemoveParentWhileRemovingChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

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
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(child_device->device));
  coordinator_loop()->RunUntilIdle();

  // Start removing the parent.
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(grandchild_device->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(grandchild_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(parent_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(UnbindTestCase, RemoveParentAndChildSimultaneously) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleDriverHostRequestedRemove(parent_device->device,
                                                                           false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // At the same time, have the child try to remove itself.
  ASSERT_NO_FATAL_FAILURES(
      coordinator().ScheduleDriverHostRequestedRemove(child_device->device, false /* do_unbind */));
  coordinator_loop()->RunUntilIdle();

  // The child device will not reply, as it already called device_remove previously.
  ASSERT_NO_FATAL_FAILURES(child_device->CheckUnbindReceived());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(parent_device->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(child_device->SendUnbindReply());
}

// This tests force removing a device before running the remove task.
TEST_F(UnbindTestCase, ForcedRemovalBeforeRemoveTask) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent", 0 /* protocol id */, "", &parent_index));

  auto* parent_device = device(parent_index);

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(parent_device->device, "child", 0 /* protocol id */, "", &child_index));

  auto* child_device = device(child_index);

  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(parent_device->device));
  coordinator_loop()->RunUntilIdle();

  // Complete the unbind without running the remove task yet.
  ASSERT_OK(child_device->device->CompleteUnbind(ZX_OK));
  ASSERT_OK(coordinator().RemoveDevice(child_device->device, true /* forced */));

  // The remove task should now be run.
  coordinator_loop()->RunUntilIdle();

  // Since we force removed the child, the parent should be dead too since it is
  // in the same devhost.
  ASSERT_EQ(Device::State::kDead, parent_device->device->state());
  ASSERT_NULL(parent_device->device->GetActiveUnbind());
  ASSERT_NULL(parent_device->device->GetActiveRemove());

  ASSERT_EQ(Device::State::kDead, child_device->device->state());
  ASSERT_NULL(child_device->device->GetActiveUnbind());
  ASSERT_NULL(child_device->device->GetActiveRemove());
}
