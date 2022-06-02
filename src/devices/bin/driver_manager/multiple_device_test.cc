// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiple_device_test.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <zircon/errors.h>

#include <string>

#include "component_lifecycle.h"
#include "coordinator_test_mock_power_manager.h"
#include "src/devices/lib/log/log.h"

TEST_F(MultipleDeviceTestCase, UnbindThenSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  ASSERT_NO_FATAL_FAILURE(
      coordinator().device_manager()->ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // The child should be unbound first.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckUnbindReceived());
  coordinator_loop()->RunUntilIdle();

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  ASSERT_NO_FATAL_FAILURE(device(child_index)->SendUnbindReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // The suspend task should complete but not send a suspend message.
  ASSERT_FALSE(device(parent_index)->HasPendingMessages());
  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, SuspendThenUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckSuspendReceived(flags));
  ASSERT_NO_FATAL_FAILURE(
      coordinator().device_manager()->ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // Check that the child device has not yet started unbinding.
  ASSERT_FALSE(device(child_index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(child_index)->SendSuspendReply(ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The parent should not have received a suspend. It is in process of removal.
  ASSERT_FALSE(device(parent_index)->HasPendingMessages());

  // Finish unbinding the child.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The parent should now be removed.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, ConcurrentSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  zx_status_t first_suspend_status = ZX_ERR_INTERNAL;
  ASSERT_NO_FATAL_FAILURE(
      DoSuspendWithCallback(flags, [&first_suspend_status](zx_status_t completion_status) {
        first_suspend_status = completion_status;
      }));

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckSuspendReceived(flags));

  zx_status_t second_suspend_status = ZX_OK;
  ASSERT_NO_FATAL_FAILURE(
      DoSuspendWithCallback(flags, [&second_suspend_status](zx_status_t completion_status) {
        second_suspend_status = completion_status;
      }));
  ASSERT_EQ(second_suspend_status, ZX_ERR_UNAVAILABLE);
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->SendSuspendReply(ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(sys_proxy()->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_EQ(first_suspend_status, ZX_OK);
}

TEST_F(MultipleDeviceTestCase, UnbindThenResume) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURE(
      coordinator().device_manager()->ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();
  // The child should be unbound first.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckUnbindReceived());

  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn));

  ASSERT_NO_FATAL_FAILURE(
      sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(
      platform_bus()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(
      device(parent_index)->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->SendUnbindReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // The resume task should complete but not send a resume message.
  ASSERT_FALSE(device(parent_index)->HasPendingMessages());
  ASSERT_FALSE(device(child_index)->HasPendingMessages());
}

TEST_F(MultipleDeviceTestCase, ResumeThenUnbind) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn));

  ASSERT_NO_FATAL_FAILURE(
      sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(
      platform_bus()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Don't reply to the resume yet.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckResumeReceived(SystemPowerState::kFullyOn));

  ASSERT_NO_FATAL_FAILURE(
      coordinator().device_manager()->ScheduleRemove(device(parent_index)->device));
  coordinator_loop()->RunUntilIdle();

  // Check that the child device has not yet started unbinding.
  ASSERT_FALSE(device(child_index)->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->SendResumeReply(ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The Child should have started resuming now. Complete resume of child.
  ASSERT_NO_FATAL_FAILURE(
      device(child_index)->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Since the resume is complete, unbinding the child should start now.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // The parent should now be removed.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
}

TEST_F(MultipleDeviceTestCase, SuspendThenResume) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  const uint32_t flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  // Don't reply to the suspend yet.
  ASSERT_NO_FATAL_FAILURE(device(child_index)->CheckSuspendReceived(flags));

  // This should return without scheduling resume tasks since suspend is in
  // progress.
  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(device(child_index)->SendSuspendReply(ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // The parent should have started suspending.
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckSuspendReceivedAndReply(flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(platform_bus()->CheckSuspendReceivedAndReply(flags, ZX_OK));
  ASSERT_FALSE(device(parent_index)->HasPendingMessages());
  ASSERT_FALSE(device(child_index)->HasPendingMessages());
  ASSERT_EQ(device(parent_index)->device->state(), Device::State::kSuspended);
  ASSERT_EQ(device(child_index)->device->state(), Device::State::kSuspended);
}

TEST_F(MultipleDeviceTestCase, ResumeThenSuspend) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURE(
      AddDevice(platform_bus()->device, "parent-device", 0 /* protocol id */, "", &parent_index));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(device(parent_index)->device, "child-device",
                                    0 /* protocol id */, "", &child_index));

  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  device(parent_index)->device->set_state(Device::State::kSuspended);
  device(child_index)->device->set_state(Device::State::kSuspended);

  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(
      sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(
      platform_bus()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  // Dont reply yet for the resume
  ASSERT_NO_FATAL_FAILURE(device(parent_index)->CheckResumeReceived(SystemPowerState::kFullyOn));
  coordinator_loop()->RunUntilIdle();

  const uint32_t flags = DEVICE_SUSPEND_FLAG_SUSPEND_RAM;
  // Should be a no-op because resume is in progress.
  ASSERT_NO_FATAL_FAILURE(DoSuspend(flags));

  ASSERT_NO_FATAL_FAILURE(device(parent_index)->SendResumeReply(ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(
      device(child_index)->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();
  ASSERT_FALSE(device(parent_index)->HasPendingMessages());
  ASSERT_FALSE(device(child_index)->HasPendingMessages());
  ASSERT_EQ(device(parent_index)->device->state(), Device::State::kActive);
  ASSERT_EQ(device(child_index)->device->state(), Device::State::kActive);
}

TEST_F(MultipleDeviceTestCase, DISABLED_ResumeTimeout) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  async::Loop driver_host_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  ASSERT_OK(driver_host_loop.StartThread("DriverHostLoop"));

  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);

  std::atomic<bool> resume_callback_executed = false;
  zx::event resume_received_event;
  zx::event::create(0, &resume_received_event);

  ResumeCallback callback = [&resume_callback_executed,
                             &resume_received_event](zx_status_t status) {
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT);
    resume_callback_executed = true;
    resume_received_event.signal(0, ZX_USER_SIGNAL_0);
  };

  ASSERT_NO_FATAL_FAILURE(DoResume(SystemPowerState::kFullyOn, std::move(callback)));

  // Dont reply for sys proxy resume. we should timeout
  async::Wait resume_task_sys_proxy(
      sys_proxy()->controller_server.channel().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        ASSERT_NO_FATAL_FAILURE(
            sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
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
      platform_bus()->controller_server.channel().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        platform_bus()->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_MEXEC, ZX_OK);
      });
  ASSERT_OK(suspend_task_pbus.Begin(devhost_loop.dispatcher()));

  async::Wait suspend_task_sys(
      sys_proxy()->controller_server.channel().get(), ZX_CHANNEL_READABLE, 0,
      [this](async_dispatcher_t*, async::Wait*, zx_status_t, const zx_packet_signal_t*) {
        sys_proxy()->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_MEXEC, ZX_OK);
      });
  ASSERT_OK(suspend_task_sys.Begin(devhost_loop.dispatcher()));

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  auto lifecycle_endpoints = fidl::CreateEndpoints<fuchsia_process_lifecycle::Lifecycle>();
  ASSERT_OK(lifecycle_endpoints.status_value());
  SuspendCallback suspend_callback = [&event](zx_status_t status) {
    event.signal(0, ZX_USER_SIGNAL_0);
  };
  ASSERT_OK(devmgr::ComponentLifecycleServer::Create(
      coordinator_loop()->dispatcher(), &coordinator(), std::move(lifecycle_endpoints->server),
      std::move(suspend_callback)));
  auto client = fidl::BindSyncClient(std::move(lifecycle_endpoints->client));
  auto result = client->Stop();
  ASSERT_OK(result.status());
  event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
  ASSERT_FALSE(suspend_task_pbus.is_pending());
  ASSERT_FALSE(suspend_task_sys.is_pending());
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_fidl) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);
  auto endpoints = fidl::CreateEndpoints<fuchsia_device_manager::SystemStateTransition>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), &coordinator(),
                                       std::move(endpoints->server), &state_mgr));
  coordinator().set_system_state_manager(std::move(state_mgr));
  auto response = fidl::WireCall(endpoints->client)
                      ->SetTerminationSystemState(
                          fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kPoweroff);

  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->is_error()) {
    call_status = response->error_value();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(coordinator().shutdown_system_state(),
            fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kPoweroff);
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_svchost_fidl) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  auto service_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(service_endpoints.status_value());

  svc::Outgoing outgoing{coordinator_loop()->dispatcher()};
  ASSERT_OK(coordinator().InitOutgoingServices(outgoing.svc_dir()));
  ASSERT_OK(outgoing.Serve(std::move(service_endpoints->server)));

  auto client_end = service::ConnectAt<fuchsia_device_manager::SystemStateTransition>(
      service_endpoints->client,
      fidl::DiscoverableProtocolDefaultPath<fuchsia_device_manager::SystemStateTransition>);
  ASSERT_OK(client_end.status_value());

  auto response = fidl::WireCall(*client_end)
                      ->SetTerminationSystemState(
                          fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kMexec);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->is_error()) {
    call_status = response->error_value();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(coordinator().shutdown_system_state(),
            fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kMexec);
}

TEST_F(MultipleDeviceTestCase, SetTerminationSystemState_fidl_wrong_state) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  auto endpoints = fidl::CreateEndpoints<fuchsia_device_manager::SystemStateTransition>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), &coordinator(),
                                       std::move(endpoints->server), &state_mgr));
  coordinator().set_system_state_manager(std::move(state_mgr));

  auto response = fidl::WireCall(endpoints->client)
                      ->SetTerminationSystemState(
                          fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kFullyOn);

  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->is_error()) {
    call_status = response->error_value();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);
  // Default shutdown_system_state in test is MEXEC.
  ASSERT_EQ(coordinator().shutdown_system_state(),
            fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kMexec);
}

TEST_F(MultipleDeviceTestCase, PowerManagerRegistration) {
  ASSERT_OK(coordinator_loop()->StartThread("DevCoordLoop"));
  set_coordinator_loop_thread_running(true);

  auto endpoints = fidl::CreateEndpoints<fuchsia_device_manager::SystemStateTransition>();
  ASSERT_OK(endpoints.status_value());

  std::unique_ptr<SystemStateManager> state_mgr;
  ASSERT_OK(SystemStateManager::Create(coordinator_loop()->dispatcher(), &coordinator(),
                                       std::move(endpoints->server), &state_mgr));
  coordinator().set_system_state_manager(std::move(state_mgr));

  MockPowerManager mock_power_manager;
  auto power_endpoints = fidl::CreateEndpoints<fuchsia_power_manager::DriverManagerRegistration>();
  ASSERT_OK(power_endpoints.status_value());

  auto dev_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(dev_endpoints.status_value());

  ASSERT_OK(fidl::BindSingleInFlightOnly(coordinator_loop()->dispatcher(),
                                         std::move(power_endpoints->server), &mock_power_manager));
  coordinator().RegisterWithPowerManager(
      std::move(power_endpoints->client), std::move(endpoints->client),
      std::move(dev_endpoints->client), [](zx_status_t status) { ASSERT_OK(status); });
  mock_power_manager.wait_until_register_called();
}

TEST_F(MultipleDeviceTestCase, DevfsWatcherCleanup) {
  Devnode* root_node = coordinator().root_device()->self;
  ASSERT_FALSE(devfs_has_watchers(root_node));

  // Create the watcher and make sure it's been registered.
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::DirectoryWatcher>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(
      devfs_watch(root_node, std::move(endpoints->server), fuchsia_io::wire::WatchMask::kAdded));
  ASSERT_TRUE(devfs_has_watchers(root_node));

  // Free our channel and make sure it gets de-registered.
  endpoints->client.reset();
  coordinator_loop()->RunUntilIdle();
  ASSERT_FALSE(devfs_has_watchers(root_node));
}

// This functor accepts a |fidl::WireUnownedResult<FidlMethod>&| and checks that
// the call completed with an application error |s| of |ZX_ERR_NOT_SUPPORTED|.
class UnsupportedEpitaphMatcher {
 public:
  template <typename FidlMethod>
  void operator()(fidl::WireUnownedResult<FidlMethod>& result) {
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().s, ZX_ERR_NOT_SUPPORTED);
  }
};

class UnsupportedErrorMatcher {
 public:
  template <typename FidlMethod>
  void operator()(fidl::WireUnownedResult<FidlMethod>& result) {
    ASSERT_OK(result.status());
    ASSERT_TRUE(result->is_error());
    ASSERT_STATUS(result->error_value(), ZX_ERR_NOT_SUPPORTED);
  }
};

TEST_F(MultipleDeviceTestCase, DevfsUnsupportedAPICheck) {
  zx::channel chan = devfs_root_clone();
  fidl::WireClient client(fidl::ClientEnd<fuchsia_io::Directory>(std::move(chan)),
                          coordinator_loop()->dispatcher());

  {
    zx::channel s, c;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &s, &c));
    client->Link("", std::move(s), "").ThenExactlyOnce(UnsupportedEpitaphMatcher());
  }
  {
    zx::event e;
    fuchsia_io::wire::Directory2RenameResult x;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &e));
    client->Rename("", std::move(e), "")
        .ThenExactlyOnce([](fidl::WireUnownedResult<fuchsia_io::Directory::Rename>& ret) {
          ASSERT_OK(ret.status());
          ASSERT_TRUE(ret->is_error());
          ASSERT_EQ(ret->error_value(), ZX_ERR_NOT_SUPPORTED);
        });
  }
  client->GetToken().ThenExactlyOnce(UnsupportedEpitaphMatcher());
  client->SetAttr({}, {}).ThenExactlyOnce(UnsupportedEpitaphMatcher());
  client->Sync().ThenExactlyOnce(UnsupportedErrorMatcher());

  coordinator_loop()->RunUntilIdle();
}

// Check that UnregisterSystemStorageForShutdown works when no system devices exist.
TEST_F(MultipleDeviceTestCase, UnregisterSystemStorageForShutdown_NoSystemDevices) {
  bool finished = false;
  zx_status_t remove_status;
  coordinator().suspend_resume_manager()->suspend_handler().UnregisterSystemStorageForShutdown(
      [&](zx_status_t status) {
        finished = true;
        remove_status = status;
      });
  coordinator_loop()->RunUntilIdle();
  ASSERT_TRUE(finished);
  ASSERT_EQ(remove_status, ZX_OK);
}

// Check that UnregisterSystemStorageForShutdown removes system devices but not boot devices.
TEST_F(MultipleDeviceTestCase, UnregisterSystemStorageForShutdown_DevicesRemoveCorrectly) {
  // Create a system device.
  size_t system_device_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "system-1", 0 /* protocol id */,
                                    "/system/driver/my-device.so", &system_device_index));
  fbl::RefPtr<Device> system_device = device(system_device_index)->device;

  // Create a child of the system device that lives in boot.
  size_t child_boot_device_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(system_device, "boot-1", 0 /* protocol id */,
                                    "/boot/driver/my-device.so", &child_boot_device_index));
  fbl::RefPtr<Device> child_boot_device = device(child_boot_device_index)->device;

  // Create a child of the system device that lives in system.
  size_t child_system_device_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(system_device, "system-2", 0 /* protocol id */,
                                    "/system/driver/my-device.so", &child_system_device_index));
  fbl::RefPtr<Device> child_system_device = device(child_system_device_index)->device;

  // Create a boot device.
  size_t boot_device_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "boot-2", 0 /* protocol id */,
                                    "/boot/driver/my-device.so", &boot_device_index));
  fbl::RefPtr<Device> boot_device = device(boot_device_index)->device;

  // Create a child of the boot that lives in system.
  size_t boot_child_system_device_index;
  ASSERT_NO_FATAL_FAILURE(AddDevice(platform_bus()->device, "system-3", 0 /* protocol id */,
                                    "/system/driver/my-device.so",
                                    &boot_child_system_device_index));
  fbl::RefPtr<Device> boot_child_system_device = device(boot_child_system_device_index)->device;

  coordinator_loop()->RunUntilIdle();

  bool finished = false;
  zx_status_t remove_status;
  coordinator().suspend_resume_manager()->suspend_handler().UnregisterSystemStorageForShutdown(
      [&](zx_status_t status) {
        finished = true;
        remove_status = status;
      });
  coordinator_loop()->RunUntilIdle();

  // Respond to Suspends. Go children then parents.
  ASSERT_NO_FATAL_FAILURE(device(boot_child_system_device_index)
                              ->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_REBOOT, ZX_OK));
  ASSERT_NO_FATAL_FAILURE(device(child_system_device_index)
                              ->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_REBOOT, ZX_OK));
  ASSERT_NO_FATAL_FAILURE(device(child_boot_device_index)
                              ->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_REBOOT, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURE(
      device(system_device_index)->CheckSuspendReceivedAndReply(DEVICE_SUSPEND_FLAG_REBOOT, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Check that the callback was called.
  ASSERT_TRUE(finished);
  ASSERT_EQ(remove_status, ZX_OK);

  // Check that our devices were suspended.
  ASSERT_EQ(system_device->state(), Device::State::kSuspended);
  ASSERT_EQ(child_boot_device->state(), Device::State::kSuspended);
  ASSERT_EQ(child_system_device->state(), Device::State::kSuspended);
  ASSERT_EQ(boot_child_system_device->state(), Device::State::kSuspended);
}
