// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

#include <zircon/errors.h>

#include <string>

#include "component_lifecycle.h"
#include "coordinator_test_mock_power_manager.h"
#include "src/devices/lib/log/log.h"

TEST_F(MultipleDeviceTestCase, UnbindThenSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  zx_txid_t txid;
  // The child should be unbound first.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(device(child_index)->controller_remote, &txid));
  coordinator_loop()->RunUntilIdle();

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(device(child_index)->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // The suspend task should complete but not send a suspend message.
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->controller_remote));
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, SuspendThenUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  zx_txid_t txid;

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceived(device(child_index)->controller_remote, flags, &txid));
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // Check that the child device has not yet started unbinding.
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(child_index)->controller_remote, ZX_OK, txid));
  coordinator_loop()->RunUntilIdle();

  // The parent should not have received a suspend. It is in process of removal.
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->controller_remote));

  // Finish unbinding the child.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The parent should now be removed.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, ConcurrentSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  zx_status_t first_suspend_status = ZX_ERR_INTERNAL;
  ASSERT_NO_FATAL_FAILURES(
      DoSuspendWithCallback(flags, [&first_suspend_status](zx_status_t completion_status) {
        first_suspend_status = completion_status;
      }));

  zx_txid_t txid;

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceived(device(child_index)->controller_remote, flags, &txid));

  zx_status_t second_suspend_status = ZX_OK;
  ASSERT_NO_FATAL_FAILURES(
      DoSuspendWithCallback(flags, [&second_suspend_status](zx_status_t completion_status) {
        second_suspend_status = completion_status;
      }));
  ASSERT_EQ(second_suspend_status, ZX_ERR_ALREADY_EXISTS);
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(child_index)->controller_remote, ZX_OK, txid));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(device(parent_index)->controller_remote, flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(sys_proxy_controller_remote_, flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(first_suspend_status, ZX_OK);
}

TEST_F(MultipleDeviceTestCase, UnbindThenResume) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  coordinator_.sys_device()->set_state(Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();
  // The child should be unbound first.
  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(device(child_index)->controller_remote, &txid));

  ASSERT_NO_FATAL_FAILURES(DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));

  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      sys_proxy_controller_remote_, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      platform_bus_controller_remote(), SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(device(parent_index)->controller_remote,
                                               SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON,
                                               ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(device(child_index)->controller_remote, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // The resume task should complete but not send a resume message.
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->controller_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));
}

TEST_F(MultipleDeviceTestCase, ResumeThenUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  coordinator_.sys_device()->set_state(Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURES(DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));

  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      sys_proxy_controller_remote_, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      platform_bus_controller_remote(), SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Don't reply to the resume yet.
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(device(parent_index)->controller_remote,
                                               SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON,
                                               &txid));
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // Check that the child device has not yet started unbinding.
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));

  ASSERT_NO_FATAL_FAILURES(SendResumeReply(device(parent_index)->controller_remote, ZX_OK, txid));
  coordinator_loop()->RunUntilIdle();

  // The Child should have started resuming now. Complete resume of child.
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(device(child_index)->controller_remote,
                                               SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON,
                                               ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Since the resume is complete, unbinding the child should start now.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(child_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();

  // The parent should now be removed.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device(parent_index)->controller_remote));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, SuspendThenResume) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  zx_txid_t txid;
  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceived(device(child_index)->controller_remote, flags, &txid));

  // This should return without scheduling resume tasks since suspend is in
  // progress.
  ASSERT_NO_FATAL_FAILURES(DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(SendSuspendReply(device(child_index)->controller_remote, ZX_OK, txid));
  coordinator_loop()->RunUntilIdle();

  // The parent should have started suspending.
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(device(parent_index)->controller_remote, flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), flags, ZX_OK));
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->controller_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));
  ASSERT_EQ(device(parent_index)->device->state(), Device::State::kSuspended);
  ASSERT_EQ(device(child_index)->device->state(), Device::State::kSuspended);
}

TEST_F(MultipleDeviceTestCase, ResumeThenSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(device(parent_index)->device, "child-device",
                                     0 /* protocol id */, "", &child_index));

  coordinator_.sys_device()->set_state(Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURES(DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      sys_proxy_controller_remote_, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
      platform_bus_controller_remote(), SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Dont reply yet for the resume
  zx_txid_t txid;
  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(device(parent_index)->controller_remote,
                                               SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON,
                                               &txid));
  coordinator_loop()->RunUntilIdle();

  const uint32_t flags = DEVICE_SUSPEND_FLAG_SUSPEND_RAM;
  // Should be a no-op because resume is in progress.
  ASSERT_NO_FATAL_FAILURES(DoSuspend(flags));

  ASSERT_NO_FATAL_FAILURES(SendResumeReply(device(parent_index)->controller_remote, ZX_OK, txid));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(device(child_index)->controller_remote,
                                               SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON,
                                               ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_FALSE(DeviceHasPendingMessages(device(parent_index)->controller_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(device(child_index)->controller_remote));
  ASSERT_EQ(device(parent_index)->device->state(), Device::State::kActive);
  ASSERT_EQ(device(child_index)->device->state(), Device::State::kActive);
}

TEST_F(MultipleDeviceTestCase, DISABLED_ResumeTimeout) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  async::Loop driver_host_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  ASSERT_OK(driver_host_loop.StartThread("DriverHostLoop"));

  coordinator_.sys_device()->set_state(Device::State::kSuspended);
  coordinator_.sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->set_state(Device::State::kSuspended);

  bool resume_callback_executed = false;
  zx::event resume_received_event;
  zx::event::create(0, &resume_received_event);

  ResumeCallback callback = [&resume_callback_executed,
                             &resume_received_event](zx_status_t status) {
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
    resume_callback_executed = true;
    resume_received_event.signal(0, ZX_USER_SIGNAL_0);
  };

  ASSERT_NO_FATAL_FAILURES(
      DoResume(SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, std::move(callback)));

  // Dont reply for sys proxy resume. we should timeout
  async::Wait resume_task_sys_proxy(
      sys_proxy_controller_remote_.get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t *, async::Wait *, zx_status_t, const zx_packet_signal_t *) {
        zx_txid_t txid;
        ASSERT_NO_FATAL_FAILURES(CheckResumeReceived(
            sys_proxy_controller_remote_, SystemPowerState::SYSTEM_POWER_STATE_FULLY_ON, &txid));
      });
  ASSERT_OK(resume_task_sys_proxy.Begin(driver_host_loop.dispatcher()));

  // Wait for the event that the callback sets, otherwise the test will quit.
  resume_received_event.wait_one(ZX_USER_SIGNAL_0, zx::time(ZX_TIME_INFINITE), nullptr);
  ASSERT_TRUE(resume_callback_executed);
}

// Test devices are suspended when component lifecycle stop event is received.
TEST_F(MultipleDeviceTestCase, ComponentLifecycleStop) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  async::Loop devhost_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  ASSERT_OK(devhost_loop.StartThread("DevHostLoop"));

  async::Wait suspend_task_pbus(
      platform_bus_controller_remote().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t *, async::Wait *, zx_status_t, const zx_packet_signal_t *) {
        CheckSuspendReceivedAndReply(platform_bus_controller_remote(), DEVICE_SUSPEND_FLAG_MEXEC,
                                     ZX_OK);
      });
  ASSERT_OK(suspend_task_pbus.Begin(devhost_loop.dispatcher()));

  async::Wait suspend_task_sys(
      sys_proxy_controller_remote_.get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t *, async::Wait *, zx_status_t, const zx_packet_signal_t *) {
        CheckSuspendReceivedAndReply(sys_proxy_controller_remote_, DEVICE_SUSPEND_FLAG_MEXEC,
                                     ZX_OK);
      });
  ASSERT_OK(suspend_task_sys.Begin(devhost_loop.dispatcher()));

  zx::channel component_lifecycle_client, component_lifecycle_server;
  ASSERT_OK(zx::channel::create(0, &component_lifecycle_client, &component_lifecycle_server));
  ASSERT_OK(devmgr::ComponentLifecycleServer::Create(
      coordinator_loop()->dispatcher(), coordinator(), std::move(component_lifecycle_server)));
  llcpp::fuchsia::process::lifecycle::Lifecycle::SyncClient client(
      std::move(component_lifecycle_client));
  auto result = client.Stop();
  ASSERT_OK(result.status());
  // the lifecycle channel should be closed now
  zx_signals_t pending;
  EXPECT_OK(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &pending));
  EXPECT_TRUE(pending & ZX_CHANNEL_PEER_CLOSED);
  ASSERT_FALSE(suspend_task_pbus.is_pending());
  ASSERT_FALSE(suspend_task_sys.is_pending());
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_fidl) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);
  zx::channel system_state_transition_client, system_state_transition_server;
  ASSERT_OK(
      zx::channel::create(0, &system_state_transition_client, &system_state_transition_server));

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), coordinator(),
                                       std::move(system_state_transition_server), &state_mgr));
  coordinator()->set_system_state_manager(std::move(state_mgr));
  auto response =
      llcpp::fuchsia::device::manager::SystemStateTransition::Call::SetTerminationSystemState(
          zx::unowned_channel(system_state_transition_client.get()),
          llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::POWEROFF);

  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(coordinator()->shutdown_system_state(),
            llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::POWEROFF);
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_svchost_fidl) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  zx::channel services, services_remote;
  ASSERT_OK(zx::channel::create(0, &services, &services_remote));

  svc::Outgoing outgoing{coordinator_loop()->dispatcher()};
  ASSERT_OK(coordinator()->InitOutgoingServices(outgoing.svc_dir()));
  ASSERT_OK(outgoing.Serve(std::move(services_remote)));

  zx::channel channel, channel_remote;
  ASSERT_OK(zx::channel::create(0, &channel, &channel_remote));
  std::string svc_dir = "/svc/";
  std::string service = svc_dir + llcpp::fuchsia::device::manager::SystemStateTransition::Name;
  ASSERT_OK(fdio_service_connect_at(services.get(), service.c_str(), channel_remote.release()));

  auto response =
      llcpp::fuchsia::device::manager::SystemStateTransition::Call::SetTerminationSystemState(
          zx::unowned_channel(channel.get()),
          llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::MEXEC);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(coordinator()->shutdown_system_state(),
            llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::MEXEC);
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_fidl_wrong_state) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  zx::channel system_state_transition_client, system_state_transition_server;
  ASSERT_OK(
      zx::channel::create(0, &system_state_transition_client, &system_state_transition_server));

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), coordinator(),
                                       std::move(system_state_transition_server), &state_mgr));
  coordinator()->set_system_state_manager(std::move(state_mgr));

  auto response =
      llcpp::fuchsia::device::manager::SystemStateTransition::Call::SetTerminationSystemState(
          zx::unowned_channel(system_state_transition_client.get()),
          llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::FULLY_ON);

  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);
  // Default shutdown_system_state in test is MEXEC.
  ASSERT_EQ(coordinator()->shutdown_system_state(),
            llcpp::fuchsia::hardware::power::statecontrol::SystemPowerState::MEXEC);
}

TEST_F(MultipleDeviceTestCase, PowerManagerRegistration) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  zx::channel system_state_transition_client, system_state_transition_server;
  ASSERT_OK(
      zx::channel::create(0, &system_state_transition_client, &system_state_transition_server));

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), coordinator(),
                                       std::move(system_state_transition_server), &state_mgr));
  coordinator()->set_system_state_manager(std::move(state_mgr));

  MockPowerManager mock_power_manager;
  zx::channel mock_power_manager_client, mock_power_manager_server;
  zx::channel dev_local, dev_remote;
  ASSERT_OK(zx::channel::create(0, &dev_local, &dev_remote));
  ASSERT_OK(zx::channel::create(0, &mock_power_manager_client, &mock_power_manager_server));
  ASSERT_OK(fidl::BindSingleInFlightOnly(
      coordinator_loop()->dispatcher(), std::move(mock_power_manager_server), &mock_power_manager));
  ASSERT_OK(coordinator()->RegisterWithPowerManager(std::move(mock_power_manager_client),
                                                    std::move(system_state_transition_client),
                                                    std::move(dev_local)));
  ASSERT_TRUE(mock_power_manager.register_called());
}
